"""Read-only Windows HID inventory and passive capture for ATORCH T3P.

Uses Windows HID APIs directly; never sends output/feature reports.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import datetime
import json
from pathlib import Path
import time


class GUID(C.Structure):
    _fields_ = [('data1', W.DWORD), ('data2', W.WORD), ('data3', W.WORD), ('data4', C.c_ubyte * 8)]


class INTERFACE(C.Structure):
    _fields_ = [('cbSize', W.DWORD), ('guid', GUID), ('flags', W.DWORD), ('reserved', C.c_size_t)]


class ATTRIBUTES(C.Structure):
    _fields_ = [('Size', W.ULONG), ('VendorID', W.USHORT), ('ProductID', W.USHORT), ('VersionNumber', W.USHORT)]


class CAPS(C.Structure):
    _fields_ = [(n, W.USHORT) for n in ('Usage', 'UsagePage', 'InputReportByteLength', 'OutputReportByteLength', 'FeatureReportByteLength')]
    _fields_ += [('Reserved', W.USHORT * 17)]
    _fields_ += [(n, W.USHORT) for n in ('NumberLinkCollectionNodes', 'NumberInputButtonCaps', 'NumberInputValueCaps', 'NumberInputDataIndices', 'NumberOutputButtonCaps', 'NumberOutputValueCaps', 'NumberOutputDataIndices', 'NumberFeatureButtonCaps', 'NumberFeatureValueCaps', 'NumberFeatureDataIndices')]


class OVERLAPPED(C.Structure):
    _fields_ = [('Internal', C.c_size_t), ('InternalHigh', C.c_size_t), ('Offset', W.DWORD), ('OffsetHigh', W.DWORD), ('hEvent', W.HANDLE)]


def api(dll, name, restype, *argtypes):
    fn = getattr(dll, name)
    fn.restype, fn.argtypes = restype, list(argtypes)
    return fn


hid = C.WinDLL('hid', use_last_error=True)
setup = C.WinDLL('setupapi', use_last_error=True)
kernel = C.WinDLL('kernel32', use_last_error=True)
get_guid = api(hid, 'HidD_GetHidGuid', None, C.POINTER(GUID))
get_set = api(setup, 'SetupDiGetClassDevsW', W.HANDLE, C.POINTER(GUID), W.LPCWSTR, W.HWND, W.DWORD)
enum_if = api(setup, 'SetupDiEnumDeviceInterfaces', W.BOOL, W.HANDLE, C.c_void_p, C.POINTER(GUID), W.DWORD, C.POINTER(INTERFACE))
detail_if = api(setup, 'SetupDiGetDeviceInterfaceDetailW', W.BOOL, W.HANDLE, C.POINTER(INTERFACE), C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p)
destroy_set = api(setup, 'SetupDiDestroyDeviceInfoList', W.BOOL, W.HANDLE)
create_file = api(kernel, 'CreateFileW', W.HANDLE, W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE)
close = api(kernel, 'CloseHandle', W.BOOL, W.HANDLE)
attributes = api(hid, 'HidD_GetAttributes', W.BOOL, W.HANDLE, C.POINTER(ATTRIBUTES))
preparsed = api(hid, 'HidD_GetPreparsedData', W.BOOL, W.HANDLE, C.POINTER(C.c_void_p))
free_preparsed = api(hid, 'HidD_FreePreparsedData', W.BOOL, C.c_void_p)
get_caps = api(hid, 'HidP_GetCaps', W.LONG, C.c_void_p, C.POINTER(CAPS))
create_event = api(kernel, 'CreateEventW', W.HANDLE, C.c_void_p, W.BOOL, W.BOOL, W.LPCWSTR)
reset_event = api(kernel, 'ResetEvent', W.BOOL, W.HANDLE)
read_file = api(kernel, 'ReadFile', W.BOOL, W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.POINTER(OVERLAPPED))
wait = api(kernel, 'WaitForSingleObject', W.DWORD, W.HANDLE, W.DWORD)
get_result = api(kernel, 'GetOverlappedResult', W.BOOL, W.HANDLE, C.POINTER(OVERLAPPED), C.POINTER(W.DWORD), W.BOOL)
cancel = api(kernel, 'CancelIoEx', W.BOOL, W.HANDLE, C.POINTER(OVERLAPPED))
INVALID = C.c_void_p(-1).value


def paths():
    guid = GUID()
    get_guid(C.byref(guid))
    devs = get_set(C.byref(guid), None, None, 0x12)
    if devs == INVALID:
        raise C.WinError(C.get_last_error())
    try:
        index = 0
        while True:
            item = INTERFACE(cbSize=C.sizeof(INTERFACE))
            if not enum_if(devs, None, C.byref(guid), index, C.byref(item)):
                if C.get_last_error() != 259:
                    raise C.WinError(C.get_last_error())
                return
            required = W.DWORD()
            detail_if(devs, C.byref(item), None, 0, C.byref(required), None)
            buf = C.create_string_buffer(required.value)
            C.cast(buf, C.POINTER(W.DWORD))[0] = 8 if C.sizeof(C.c_void_p) == 8 else 6
            if not detail_if(devs, C.byref(item), buf, len(buf), None, None):
                raise C.WinError(C.get_last_error())
            yield C.wstring_at(C.addressof(buf) + 4)
            index += 1
    finally:
        destroy_set(devs)


def inspect(path):
    result = {'path': path}
    handle = create_file(path, 0, 3, None, 3, 0, None)
    if handle == INVALID:
        result['open_error'] = str(C.WinError(C.get_last_error()))
        return result
    try:
        attr = ATTRIBUTES(Size=C.sizeof(ATTRIBUTES))
        if attributes(handle, C.byref(attr)):
            result.update(vid=attr.VendorID, pid=attr.ProductID, version=attr.VersionNumber)
        for key, fn in [('manufacturer', 'HidD_GetManufacturerString'), ('product', 'HidD_GetProductString'), ('serial', 'HidD_GetSerialNumberString')]:
            text = C.create_unicode_buffer(256)
            if api(hid, fn, W.BOOL, W.HANDLE, C.c_void_p, W.ULONG)(handle, text, C.sizeof(text)):
                result[key] = text.value
        pp = C.c_void_p()
        if preparsed(handle, C.byref(pp)):
            try:
                caps = CAPS()
                if get_caps(pp, C.byref(caps)) == 0x110000:
                    result['caps'] = {n: getattr(caps, n) for n, _ in CAPS._fields_ if n != 'Reserved'}
            finally:
                free_preparsed(pp)
    finally:
        close(handle)
    return result


def capture(path, size, seconds):
    handle = create_file(path, 0x80000000, 3, None, 3, 0x40000000, None)
    if handle == INVALID:
        raise C.WinError(C.get_last_error())
    event = create_event(None, True, False, None)
    if not event:
        close(handle)
        raise C.WinError(C.get_last_error())
    started = time.monotonic()
    reports = []
    try:
        while time.monotonic() - started < seconds:
            reset_event(event)
            ov = OVERLAPPED(hEvent=event)
            buf, count = C.create_string_buffer(size), W.DWORD()
            ready = read_file(handle, buf, size, C.byref(count), C.byref(ov))
            if not ready:
                if C.get_last_error() != 997:
                    raise C.WinError(C.get_last_error())
                remaining_ms = max(1, int((seconds - (time.monotonic() - started)) * 1000))
                status = wait(event, min(1000, remaining_ms))
                if status != 0:
                    cancel(handle, C.byref(ov))
                    get_result(handle, C.byref(ov), C.byref(count), True)
                    if status != 258:
                        raise OSError(f'WaitForSingleObject failed: {status}')
                    continue
                if not get_result(handle, C.byref(ov), C.byref(count), False):
                    raise C.WinError(C.get_last_error())
            reports.append({'seconds': round(time.monotonic() - started, 6), 'hex': buf.raw[:count.value].hex()})
    finally:
        close(event)
        close(handle)
    return reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vid', type=lambda s: int(s, 0), default=0x0483)
    parser.add_argument('--pid', type=lambda s: int(s, 0), default=0x5750)
    parser.add_argument('--seconds', type=float, default=0, help='Passively read input reports for this long')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 0 <= args.seconds <= 300:
        parser.error('--seconds must be between 0 and 300')
    needle = f'vid_{args.vid:04x}&pid_{args.pid:04x}'
    devices = [inspect(p) for p in paths() if needle in p.lower()]
    result = {'captured_at': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'devices': devices}
    if args.seconds:
        if len(devices) != 1 or 'caps' not in devices[0]:
            result['capture_error'] = 'Need exactly one accessible matching HID interface'
        else:
            device = devices[0]
            try:
                result['reports'] = capture(device['path'], device['caps']['InputReportByteLength'], args.seconds)
            except OSError as error:
                result['capture_error'] = str(error)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    summary = dict(result)
    if 'reports' in summary:
        summary['report_count'] = len(summary.pop('reports'))
    print(json.dumps(summary, ensure_ascii=True, indent=2))
    return 1 if 'capture_error' in result else 0


if __name__ == '__main__':
    raise SystemExit(main())
