"""Verify official SDK ROM USB_KEY clues offline; never opens any device."""
import hashlib
import json
import re
from pathlib import Path
import struct
from find_stock_calls import instructions

ROOT=Path(__file__).resolve().parents[1]
ROM_SHA='12341f7db1d764b865466c8d54447b53d8faabe9867ec728752d44085dd0c8f7'
LOADER_SHA='1295a01d5a89ad42e9eda6b87acfe7d543ab68293d4087c3dda9bf9ab472dae1'


def main():
    folder=ROOT/'local/jieli-sdk/SDK/cpu/br35/tools'
    rom=(folder/'rom.image').read_bytes()
    loader=(folder/'br35loader.bin').read_bytes()
    if len(rom)!=32768 or hashlib.sha256(rom).hexdigest()!=ROM_SHA:
        raise ValueError('Unexpected SDK ROM reference')
    if len(loader)!=27328 or hashlib.sha256(loader).hexdigest()!=LOADER_SHA:
        raise ValueError('Unexpected SDK loader reference')
    code={a:(raw,insn.strip()) for a,raw,insn,_ in instructions(ROOT/'analysis/sdk-rom.disasm.txt')}
    checks={
        0xfa094:'r0 = 0x10130c',
        0xfa09a:'r1 = 0xffff',
        0xfa0b8:'r2 = 0xfa11e',
        0xfa12e:'r4 = 0xfd3500',
        0xfa136:'r2 = 0x10130c',
        0xfa13c:'r1 = uextra(r1, p:0x1, l:0x1)',
        0xfa142:'r1 <= insert(r3, p:0x1, l:0x1f)',
        0xfa148:'r2 = 0x16ef',
        0xfa14e:'if (r1 != r2)',
        0xfa156:'r0 = 0x14',
        0xfa158:'call 0x16a0',
        0xfa15e:'[r4+0x190] = r0',
        0xfa162:'[r4+0x18c] = r0',
        0xfa166:'[r4+0x4] &= 0xFFFFFFFE',
        0xfa16a:'[r4+0x4] &= 0xFFFFFFFD',
        0xfa16e:'[r4+0x8] &= 0xFFFFFFFE',
        0xfa172:'[r4+0x8] &= 0xFFFFFFFD',
        0xfa176:'r0 = 0x1',
        0xfa178:'call 0x1726',
        0xfb7fc:'[--sp] = rets',
        0xfb836:'r1 = 0xf4240',
        0xfb8a2:'[--sp] = {rets, r4}',
        0xfb8a8:'r0 = 0x3e8',
        0xfb8ac:'call -0xb2',
    }
    evidence=[]
    for address,fragment in checks.items():
        raw,insn=code[address]
        at=address-0xf8000
        if fragment not in insn or rom[at:at+len(raw)]!=raw:
            raise ValueError(f'SDK ROM instruction mismatch at {address:#x}')
        evidence.append(dict(address=hex(address),raw_hex=raw.hex(),instruction=insn))
    # pi32v2 displacement is relative to the end of the call, not its start.
    calls=[]
    for address,target in ((0xfa158,0xfb7fc),(0xfa178,0xfb8a2),(0xfb8ac,0xfb7fc)):
        raw,insn=code[address]
        displacement=int(re.search(r'call (-?0x[0-9a-f]+)',insn)[1],16)
        if address+len(raw)+displacement!=target:
            raise ValueError('Relative delay-call target mismatch')
        calls.append(dict(address=hex(address),target=hex(target)))
    headers=ROOT/'local/jieli-sdk/SDK/interface/driver/cpu/br35/asm'
    gpio=(headers/'gpio_hw.h').read_text(encoding='utf-8')
    regs=(headers/'br35.h').read_text(encoding='utf-8')
    if not (re.search(r'#define\s+IO_PORT_DP\s+\(IO_GROUP_NUM\s*\*\s*14\s*\+\s*0\)',gpio)
            and re.search(r'#define\s+IO_PORT_DM\s+\(IO_GROUP_NUM\s*\*\s*14\s*\+\s*1\)',gpio)
            and re.search(r'#define\s+ls_base\s+0xfd0000',regs)
            and re.search(r'#define\s+map_adr\(grp, adr\)\s+\(\(64 \* grp \+ adr\) \* 4\)',regs)
            and re.search(r'#define\s+JL_PORTUSB_BASE\s+\(ls_base \+ map_adr\(0x35, 0x00\)\)',regs)
            and re.search(r'__RO __u32 IN;\s*__RW __u32 OUT;\s*__RW __u32 DIR;',regs)):
        raise ValueError('SDK USB register/bit mapping changed')
    descriptor=rom[0x2cd5:0x2cd5+18]
    if descriptor[:2]!=b'\x12\x01' or struct.unpack_from('<HH',descriptor,8)!=(0x4c4a,0x2942):
        raise ValueError('SDK ROM device descriptor mismatch')
    identity=b'BR35    UBOOT1.00       1.00'
    if rom[0x2cb5:0x2cb5+len(identity)]!=identity:
        raise ValueError('SDK ROM inquiry identity mismatch')
    # Independent arithmetic check of the 16-bit shift-register interpretation.
    key=0x16ef
    bits=[(key>>bit)&1 for bit in range(15,-1,-1)]
    received=0xffff
    for bit in bits:
        received=((received<<1)|bit)&0xffff
    if received!=key:
        raise ValueError('Key bit-order interpretation mismatch')
    result=dict(status='SDK REFERENCE ONLY; not ROM readback from the connected T3P',
        sdk_revision='d0167685d032d745d88fe50233302edd46941622',
        rom_sha256=ROM_SHA,loader_sha256=LOADER_SHA,loader_bytes=len(loader),
        sdk_rom_identity=dict(vid='4c4a',pid='2942',inquiry='BR35    UBOOT1.00       1.00'),
        usb_key=dict(hex='16ef',msb_first_bits=''.join(map(str,bits)),
                     shift_register_address='0x10130c',receiver='0xfa11e',
                     sampled_input='PORTUSB.IN bit1 = DM per SDK gpio_hw.h',
                     candidate_clock='DP; inferred other USB line, consistent with external waveform diagram',
                     prior_bench_mapping='v1..3 clock DM/data DP is opposite to SDK ROM sampled data bit; never sent to T3P'),
        delay_calls=calls,
        ack_inference=dict(pre_output_delay_argument=20,pre_output_delay_routine='0xfb7fc',
                           output_low_hold_ms_argument=1,millisecond_routine='0xfb8a2',
                           microsecond_divisor=1000000,
                           note='Nominal delay-unit interpretation from timer arithmetic; not measured T3P ACK'),
        sdk_header_sha256={name:hashlib.sha256((headers/name).read_bytes()).hexdigest()
                           for name in ('gpio_hw.h','br35.h')},
        caution='External timing, electrical levels, ACK and USB handoff still need board validation',
        instruction_evidence=evidence)
    (ROOT/'analysis/rom-usb-evidence.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(f'PASS: {len(evidence)} SDK ROM instruction checks, 3 delay calls and SDK DM data mapping; USB_KEY=0x16EF')
    print('SDK reference only; T3P ROM mode has not been entered')


if __name__=='__main__':
    main()
