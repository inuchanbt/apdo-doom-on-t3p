"""Read 3.0.3 data at its runtime address, offline; never accesses USB.

Mappings come from this exact stock application's startup copy loops. They are
not universal AC707N addresses and do not imply writable/device API locations.
"""
import argparse
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT/'local/unpacked/T3P Firmware Version 3.0.3.bin/files/app.bin'
BASE = 0x0c000100
MAPPINGS = [(0x11f0e0,0x1b80,0xc161b80), (0x120c60,0x2740,0xc163700)]


def load():
    return APP.read_bytes()


def offset(address):
    for start, size, source in MAPPINGS:
        if start <= address < start+size:
            return source-BASE+address-start
    if BASE <= address < BASE+APP.stat().st_size:
        return address-BASE
    raise ValueError(f'No initialized image mapping for {address:#x}')


def read(data, address, size):
    pos = offset(address)
    if pos+size > len(data):
        raise ValueError('Read exceeds image')
    return data[pos:pos+size]


def aliases(file_offset):
    result = [BASE+file_offset]
    for start,size,source in MAPPINGS:
        if source-BASE <= file_offset < source-BASE+size:
            result.append(start+file_offset-(source-BASE))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('address',type=lambda s:int(s,0))
    parser.add_argument('--length',type=lambda s:int(s,0),default=64)
    parser.add_argument('--find',action='store_true',help='Find little-endian pointer occurrences')
    args = parser.parse_args()
    data = load()
    if args.find:
        needle = struct.pack('<I',args.address)
        pos = -1
        while True:
            pos = data.find(needle,pos+1)
            if pos < 0:
                break
            print(' / '.join(f'{a:#010x}' for a in aliases(pos)))
    else:
        raw = read(data,args.address,args.length)
        for pos in range(0,len(raw),16):
            chunk = raw[pos:pos+16]
            words = ' '.join(f'{v:08x}' for v in struct.unpack('<'+'I'*(len(chunk)//4),chunk[:len(chunk)//4*4]))
            chars = ''.join(chr(c) if 32<=c<127 else '.' for c in chunk)
            print(f'{args.address+pos:08x}  {words:35}  {chars}')


if __name__ == '__main__':
    main()
