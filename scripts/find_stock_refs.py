"""Find candidate constant references in vendor pi32v2 disassembly.

This is a linear hint finder, not a control-flow analysis. Constants are dropped
at branches/returns and caller-saved registers at calls. Verify every hit against
the surrounding code before treating it as evidence.
"""
import argparse
from collections import deque
from pathlib import Path
import re
from find_stock_calls import instructions


def references(path, targets, before=12):
    regs = {}
    history = deque(maxlen=before)
    for address, raw, instruction, line in instructions(path):
        matches = []
        # Remove bundled-instruction annotation; the following line is parsed too.
        instruction = instruction.split('#')[0].strip()
        # Keep loads of addresses as well as actual accesses: both help locate
        # pointer arguments and table references.
        assign = re.fullmatch(r'(r\d+) = (0x[0-9a-f]+|-0x[0-9a-f]+)', instruction)
        move = re.fullmatch(r'(r\d+) = (r\d+)', instruction)
        add = re.fullmatch(r'(r\d+) = (r\d+) \+ (-?0x[0-9a-f]+)', instruction)
        update = re.fullmatch(r'(r\d+) \+= (-?0x[0-9a-f]+)', instruction)
        if assign:
            regs[assign[1]] = int(assign[2], 16) & 0xffffffff
        elif move:
            if move[2] in regs:
                regs[move[1]] = regs[move[2]]
            else:
                regs.pop(move[1], None)
        elif add:
            if add[2] in regs:
                regs[add[1]] = (regs[add[2]]+int(add[3],16)) & 0xffffffff
            else:
                regs.pop(add[1], None)
        elif update:
            if update[1] in regs:
                regs[update[1]] = (regs[update[1]]+int(update[2],16)) & 0xffffffff
        else:
            # Compute effective addresses before a load overwrites its base.
            for access in re.finditer(r'\[(r\d+)(?:\+(-?0x[0-9a-f]+))?\]', instruction):
                if access[1] in regs:
                    effective = (regs[access[1]]+int(access[2] or '0',16)) & 0xffffffff
                    if effective in targets:
                        matches.append(('memory', effective))
            dest = re.match(r'(r\d+)(?:_r\d+)? (?:=|\+=|-=|\|=|&=|\^=|<<=|>>=|<=)', instruction)
            if dest:
                regs.pop(dest[1], None)
        for match in [assign, move, add, update]:
            if match and regs.get(match[1]) in targets:
                matches.append(('address', regs[match[1]]))
        if matches:
            yield address, matches, list(history)+[line]
        if re.search(r'\bcall\b', instruction):
            for reg in ['r0','r1','r2','r3']:
                regs.pop(reg, None)
        if re.search(r'\bgoto\b|\brts\b|\bpc\b', instruction):
            regs.clear()
        history.append(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('targets', nargs='+', type=lambda s:int(s,0))
    parser.add_argument('--input', type=Path, default=Path(__file__).resolve().parents[1]/'analysis/app-3.0.3.disasm.txt')
    parser.add_argument('--before', type=int, default=12)
    args = parser.parse_args()
    for address, hits, context in references(args.input, set(args.targets), args.before):
        print('\nCANDIDATE', hex(address), ', '.join(f'{kind} {value:#x}' for kind,value in hits))
        print('\n'.join(context))


if __name__ == '__main__':
    main()
