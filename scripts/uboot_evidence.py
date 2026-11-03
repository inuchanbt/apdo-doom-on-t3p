"""Verify the stock custom HID updater's erase/write path, offline only."""
import hashlib
import json
from pathlib import Path
import re
import struct
from find_stock_calls import instructions

ROOT=Path(__file__).resolve().parents[1]
EXPECTED='53b7b7f6ec1711becb538625a5afe4b45cc3dba5fafee8bcb229bd12d0791ec1'


def main():
    paths=sorted((ROOT/'local/unpacked').glob('*/top/uboot.boot'))
    if len(paths)!=4 or any(hashlib.sha256(p.read_bytes()).hexdigest()!=EXPECTED for p in paths):
        raise ValueError('Expected all four identical stock bootloaders')
    data=paths[0].read_bytes()
    version,size,base,field=struct.unpack_from('<HHII',data)
    if (version,size,base,field)!=(1,17888,0x102600,16) or len(data)!=size+16:
        raise ValueError('Unexpected boot header')
    code={address:(raw,insn.strip()) for address,raw,insn,_ in instructions(ROOT/'analysis/uboot.disasm.txt')}
    checks={
        0x10453c:'if (r0 == 0xcc)',
        0x104540:'if (r0 != 0xaa)',
        0x104572:'if (r0 != r1)',
        0x1046da:'call -0x1836',
        0x104710:'r0 = 0xbb',
        0x102f16:'r6 = 0xd8',
        0x102f1c:'r6 = 0x20',
        0x102f32:'call -0x8ae',
        0x1047b0:'call -0x16f0',
        0x1048f2:'r0 = 0x6f6f6275',
        0x1048f8:'r1 = 0x6b6fa574',
    }
    evidence=[]
    for address,fragment in checks.items():
        raw,insn=code[address]
        if fragment not in insn or data[16+address-base:16+address-base+len(raw)]!=raw:
            raise ValueError(f'Boot disassembly mismatch: {address:#x}')
        evidence.append(dict(address=hex(address),raw_hex=raw.hex(),instruction=insn))
    # Independently resolve the two PC-relative calls; don't treat the printed
    # displacement as an absolute address.
    targets=[]
    for address,expected in [(0x1046da,0x102ea8),(0x1047b0,0x1030c4)]:
        raw,insn=code[address]
        displacement=int(re.search(r'call (-?0x[0-9a-f]+)',insn).group(1),16)
        target=address+len(raw)+displacement
        if target!=expected:
            raise ValueError('Unexpected boot call target')
        targets.append(dict(call=hex(address),target=hex(target)))
    result=dict(status='OFFLINE ONLY; updater was not entered',sha256=EXPECTED,
        identical_stock_copies=4,payload_base=hex(base),payload_bytes=size,
        application_entry_tag='ubootZst',completion_tag_hex='75626f6f74a56f6b',
        custom_hid_protocol=dict(header_byte='aa',data_byte='cc',header_ack='bb',data_ack='dd'),
        finding='Valid transfer header reaches flash erase before BB acknowledgement; not a backup command',
        caveat='No ROM recovery entry or whole-flash read command has been demonstrated on this T3P',
        instruction_evidence=evidence,call_targets=targets)
    (ROOT/'analysis/uboot-protocol.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('PASS: four identical stock updaters; validated header -> erase -> BB ack; data -> flash program')


if __name__=='__main__':
    main()
