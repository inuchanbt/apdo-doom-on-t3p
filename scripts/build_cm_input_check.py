"""Build an input-only RP2350-USB-CM diagnostic, offline after tool setup.

Never enumerates devices, copies to a removable drive, resets or flashes any
board. The output belongs to the auxiliary RP2350 board, NOT to the T3P.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def inspect_uf2(path):
    data=path.read_bytes()
    if not data or len(data)%512:
        raise ValueError('Invalid UF2 length')
    addresses=[]
    ignored=[]
    total=len(data)//512
    for index in range(total):
        at=index*512
        m0,m1,flags,address,size,block,count,family=struct.unpack_from('<8I',data,at)
        end=struct.unpack_from('<I',data,at+508)[0]
        if (m0,m1,end)!=(0x0a324655,0x9e5d5157,0x0ab16f30) or size>476:
            raise ValueError(f'Invalid UF2 block {index}')
        # picotool 2.2.0 gen_abs_block: RP2350-E10 compatibility record.
        # It has its own block count and is explicitly ignored by RP2 ROMs.
        # Accept only the exact generated shape, never arbitrary ABSOLUTE data.
        if index==0 and family==0xe48bff57:
            if ((flags,address,size,block,count)!=(0xa000,0x10ffff00,256,0,2)
                    or data[at+32:at+288]!=bytes([0xef])*256
                    or struct.unpack_from('<I',data,at+288)[0]!=0x9957e304
                    or any(data[at+292:at+508])):
                raise ValueError('Invalid RP2350-E10 ignored compatibility record')
            ignored.append(dict(address=hex(address),family=hex(family),reason='RP2350-E10 ignore-block extension'))
            continue
        payload_index=index-len(ignored)
        if (block!=payload_index or count!=total-len(ignored)
                or flags!=0x2000 or family!=0xe48bff59 or size!=256):
            raise ValueError('Unexpected UF2 numbering, flags or RP2350 Arm family')
        if address!=0x10000000+payload_index*256 or address+size>0x10200000:
            raise ValueError('UF2 payload outside the CM 2 MiB flash or noncontiguous')
        addresses.append(dict(address=address,size=size,flags=flags,family=family))
    if not addresses:
        raise ValueError('No RP2350 program blocks')
    return dict(bytes=len(data),blocks=total,program_blocks=len(addresses),
                ignored_compatibility_records=ignored,family_ids=['0xe48bff59'],
                min_address=hex(min(x['address'] for x in addresses)),
                max_end_address=hex(max(x['address']+x['size'] for x in addresses)))


def main():
    tools=ROOT/'local/tools'
    compilers=list(tools.glob('arm*/**/bin/arm-none-eabi-gcc.exe'))
    if len(compilers)!=1:
        raise SystemExit('Extract one official ARM toolchain under local/tools/arm-gcc first')
    compiler=compilers[0]
    cmake=tools/'pico-build/cmake/data/bin/cmake.exe'
    ninja=tools/'pico-build/ninja-1.11.1.3.data/scripts/ninja.exe'
    picotool=tools/'pico-build/picotool/picotool'
    source=ROOT/'recovery/cm_input_check'
    output=ROOT/'artifacts/cm-input-check-v2'
    build=output/'build'
    output.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    commands=[
        [str(cmake),'-S',str(source),'-B',str(build),'-G','Ninja',
         '-DCMAKE_MAKE_PROGRAM='+ninja.as_posix(),
         '-DPICO_TOOLCHAIN_PATH='+compiler.parent.as_posix(),
         '-DPython3_EXECUTABLE='+Path('D:/KM003C/.venv/Scripts/python.exe').as_posix(),
         '-Dpicotool_DIR='+picotool.as_posix(),'-DCMAKE_BUILD_TYPE=Release'],
        [str(cmake),'--build',str(build),'--parallel','8'],
    ]
    logs=[]
    for command in commands:
        result=subprocess.run(command,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace')
        logs.append(result.stdout+result.stderr)
        (output/'build.log').write_text('\n'.join(logs),encoding='utf-8')
        if result.returncode:
            raise SystemExit(logs[-1][-12000:])
    uf2=output/'RP2350_CM_INPUT_CHECK_ONLY.uf2'
    elf=output/'RP2350_CM_INPUT_CHECK_ONLY.elf'
    shutil.copyfile(build/'t3p_cm_input_check.uf2',uf2)
    shutil.copyfile(build/'t3p_cm_input_check.elf',elf)
    notices=output/'notices'
    notices.mkdir(exist_ok=True)
    for name,path in {
        'cm-input-check-MIT.txt':source/'LICENSE',
        'pico-sdk-BSD-3-Clause.txt':ROOT/'local/pico-sdk/LICENSE.TXT',
        'tinyusb-MIT.txt':ROOT/'local/pico-sdk/lib/tinyusb/LICENSE',
    }.items():
        shutil.copyfile(path,notices/name)
    details=inspect_uf2(uf2)
    if '0xe48bff59' not in details['family_ids']:
        raise ValueError('Missing RP2350 secure Arm family ID')
    manifest=dict(status='AUXILIARY RP2350 BOARD DIAGNOSTIC ONLY; never a T3P firmware',
        board='Waveshare RP2350-USB-CM',target='RP2350A Cortex-M33',diagnostic_version=2,
        target_port='male PIO USB; leave empty during first test',
        target_gpio=dict(dp=13,dm=12,direction='input',internal_pulls='disabled by default and after PULLCHECK'),
        features=['native USB CDC','GPIO input sampling','explicit weak-pull bench check; male port must be empty','optional self BOOTSEL reboot'],
        absent_features=['USB_KEY output','PIO USB host','T3P commands','T3P power control'],
        uf2=details,uf2_sha256=hashlib.sha256(uf2.read_bytes()).hexdigest(),
        elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
        compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(),
        source_sha256={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in sorted(source.rglob('*')) if p.is_file()})
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__':
    main()
