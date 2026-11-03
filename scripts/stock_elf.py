"""Wrap a raw pi32v2 byte range in an ELF for offline disassembly only.

This is an analysis container, not a bootable or flashable firmware image.
"""
import argparse
from pathlib import Path
import struct


def wrap(data, address):
    names = b'\0.text\0.shstrtab\0'
    text_offset = 52
    names_offset = text_offset + len(data)
    sections_offset = (names_offset + len(names) + 3) & ~3
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    header = struct.pack('<16sHHIIIIIHHHHHH', ident, 2, 0xf1, 1, address, 0,
                         sections_offset, 0, 52, 0, 0, 40, 3, 2)
    sections = bytes(40)
    sections += struct.pack('<10I', 1, 1, 6, address, text_offset, len(data), 0, 0, 2, 0)
    sections += struct.pack('<10I', 7, 3, 0, 0, names_offset, len(names), 0, 0, 1, 0)
    result = header + data + names
    result += bytes(sections_offset-len(result)) + sections
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--base', type=lambda s: int(s, 0), default=0x0c000100)
    parser.add_argument('--offset', type=lambda s: int(s, 0), default=0)
    parser.add_argument('--size', type=lambda s: int(s, 0))
    args = parser.parse_args()
    data = args.input.read_bytes()
    end = len(data) if args.size is None else args.offset + args.size
    if not 0 <= args.offset < end <= len(data) or not 0 <= args.base <= 0xffffffff:
        parser.error('Invalid range or base address')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    result = wrap(data[args.offset:end], args.base)
    args.output.write_bytes(result)
    print('Analysis ELF:', args.output, 'bytes:', end-args.offset, 'base:', hex(args.base))


if __name__ == '__main__':
    main()
