"""Locate processes holding a specific Windows device, without closing their handles.

Only matching device names and process names are printed. Object-name queries
run in daemon workers so an unrelated blocked pipe cannot stall the scan.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import queue
import threading
import time

from inspect_hid import api, kernel, close, create_file, INVALID


class ENTRY(C.Structure):
    _fields_ = [('Object', C.c_size_t), ('ProcessId', C.c_size_t), ('Handle', C.c_size_t),
                ('Access', W.ULONG), ('Creator', W.USHORT), ('Type', W.USHORT),
                ('Attributes', W.ULONG), ('Reserved', W.ULONG)]


class UNICODE(C.Structure):
    _fields_ = [('Length', W.USHORT), ('MaximumLength', W.USHORT), ('Buffer', C.c_void_p)]


nt = C.WinDLL('ntdll')
query_system = api(nt, 'NtQuerySystemInformation', W.LONG, W.ULONG, C.c_void_p, W.ULONG, C.POINTER(W.ULONG))
query_object = api(nt, 'NtQueryObject', W.LONG, W.HANDLE, W.ULONG, C.c_void_p, W.ULONG, C.POINTER(W.ULONG))
open_process = api(kernel, 'OpenProcess', W.HANDLE, W.DWORD, W.BOOL, W.DWORD)
duplicate = api(kernel, 'DuplicateHandle', W.BOOL, W.HANDLE, W.HANDLE, W.HANDLE, C.POINTER(W.HANDLE), W.DWORD, W.BOOL, W.DWORD)
current = api(kernel, 'GetCurrentProcess', W.HANDLE)()
process_name = api(kernel, 'QueryFullProcessImageNameW', W.BOOL, W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD))
get_pid = api(kernel, 'GetCurrentProcessId', W.DWORD)


def worker(requests):
    while True:
        handle, result = requests.get()
        try:
            buf = C.create_string_buffer(65536)
            required = W.ULONG()
            status = query_object(handle, 1, buf, len(buf), C.byref(required))
            name = ''
            if status >= 0:
                value = UNICODE.from_buffer(buf)
                if value.Buffer and value.Length:
                    name = C.wstring_at(value.Buffer, value.Length // 2)
            result.put(name)
        finally:
            close(handle)


def new_worker():
    requests = queue.Queue()
    threading.Thread(target=worker, args=(requests,), daemon=True).start()
    return requests


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('device_name', help=r'Exact PDO name, e.g. \Device\000001e4')
    parser.add_argument('--seconds', type=float, default=20)
    parser.add_argument('--pid', type=int, help='Restrict to one process ID')
    parser.add_argument('--contains', action='store_true', help='Match a device-name substring')
    args = parser.parse_args()
    own = create_file(str(Path(__file__).resolve()), 0, 7, None, 3, 0, None)
    if own == INVALID:
        raise C.WinError(C.get_last_error())
    try:
        size = 4 * 1024 * 1024
        while True:
            buf = C.create_string_buffer(size)
            required = W.ULONG()
            status = query_system(64, buf, size, C.byref(required))
            if status >= 0:
                break
            if status & 0xffffffff != 0xc0000004:
                raise OSError('NtQuerySystemInformation: %08x' % (status & 0xffffffff))
            size = max(size * 2, required.value + 65536)
        count = C.c_size_t.from_buffer(buf).value
        entries = (ENTRY * count).from_buffer(buf, C.sizeof(C.c_size_t) * 2)
        file_type = next(e.Type for e in entries if e.ProcessId == get_pid() and e.Handle == own)
    finally:
        close(own)
    candidates = [e for e in entries if e.Type == file_type and e.ProcessId != get_pid()
                  and (args.pid is None or e.ProcessId == args.pid)
                  and (args.pid is not None or e.Access not in (0x12019f, 0x1a019f))]
    # HID handles commonly carry FILE_READ_DATA / FILE_WRITE_DATA. Search them first.
    candidates.sort(key=lambda e: (e.Access & 0xffff != 3, -e.ProcessId))
    requests = new_worker()
    started, checked, blocked = time.monotonic(), 0, 0
    matches = set()
    for entry in candidates:
        if time.monotonic() - started > args.seconds or blocked >= 128:
            break
        process = open_process(0x40, False, entry.ProcessId)
        if not process:
            continue
        dup = W.HANDLE()
        try:
            ok = duplicate(process, entry.Handle, current, C.byref(dup), 0, False, 2)
        finally:
            close(process)
        if not ok:
            continue
        result = queue.Queue()
        requests.put((dup.value, result))
        checked += 1
        try:
            name = result.get(timeout=.03)
        except queue.Empty:
            blocked += 1
            requests = new_worker()
            continue
        if ((args.contains and args.device_name.casefold() in name.casefold())
                or name.casefold() == args.device_name.casefold()):
            matches.add(entry.ProcessId)
            print('Matching device handle: PID=%d name=%s' % (entry.ProcessId, name))
    for pid in sorted(matches):
        process = open_process(0x1000, False, pid)
        name = C.create_unicode_buffer(32768)
        length = W.DWORD(len(name))
        try:
            label = Path(name.value).name if process and process_name(process, 0, name, C.byref(length)) else '?'
        finally:
            if process:
                close(process)
        print('Device owner: PID=%d process=%s' % (pid, label))
    print('Scanned %d/%d file handles; blocked queries=%d; matching processes=%d' % (checked, len(candidates), blocked, len(matches)))


if __name__ == '__main__':
    main()
