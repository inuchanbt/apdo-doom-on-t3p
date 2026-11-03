"""Find direct pi32v2 calls to an address in an offline disassembly.

JieLi objdump prints PC-relative displacements. The target is PC plus the
instruction length plus the signed displacement, not PC plus displacement.
"""
import argparse
from pathlib import Path
import re


def instructions(path):
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        match = re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)', line)
        if match:
            address = int(match[1], 16)
            raw = bytes.fromhex(match[2])
            yield address, raw, match[3], line


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', type=lambda s: int(s, 0))
    parser.add_argument('--input', type=Path, default=Path(__file__).resolve().parents[1]/'analysis/app-3.0.3.disasm.txt')
    parser.add_argument('--before', type=int, default=10)
    parser.add_argument('--after', type=int, default=2)
    args = parser.parse_args()
    entries = list(instructions(args.input))
    for index, (address, raw, instruction, _) in enumerate(entries):
        match = re.search(r'\bcall (-?0x[0-9a-f]+)\b', instruction)
        if match and address+len(raw)+int(match[1], 16) == args.target:
            print('\nCALL TARGET', hex(args.target), 'AT', hex(address))
            print('\n'.join(e[3] for e in entries[max(0,index-args.before):index+args.after+1]))


if __name__ == '__main__':
    main()
