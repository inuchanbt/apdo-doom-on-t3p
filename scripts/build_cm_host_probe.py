"""Offline build of a manual-start CM fixed HID read-query probe; never flashes."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from build_cm_input_check import inspect_uf2

ROOT=Path(__file__).resolve().parents[1]
PIO_REV='b186e36bea0f3bcc2ee94ca1fa81e7f71281443a'


def main():
    git=Path('C:/Program Files/Git/cmd/git.exe')
    revision=subprocess.check_output([str(git),'-C',str(ROOT/'local/Pico-PIO-USB'),
                                      'rev-parse','HEAD'],text=True).strip()
    if revision!=PIO_REV: raise SystemExit('PIO USB revision differs from reviewed source')
    tools=ROOT/'local/tools'
    compiler=tools/'arm-gcc/bin/arm-none-eabi-gcc.exe'
    cmake=tools/'pico-build/cmake/data/bin/cmake.exe'
    ninja=tools/'pico-build/ninja-1.11.1.3.data/scripts/ninja.exe'
    picotool=tools/'pico-build/picotool/picotool'
    pioasm=tools/'pico-build/sdk-tools/pioasm'
    source=ROOT/'recovery/cm_host_probe'
    output=ROOT/'artifacts/cm-host-probe-v2'
    build=output/'build'
    output.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    commands=[
        [str(cmake),'-S',str(source),'-B',str(build),'-G','Ninja',
         '-DCMAKE_MAKE_PROGRAM='+ninja.as_posix(),
         '-DPICO_TOOLCHAIN_PATH='+compiler.parent.as_posix(),
         '-DPython3_EXECUTABLE=D:/KM003C/.venv/Scripts/python.exe',
         '-Dpicotool_DIR='+picotool.as_posix(),'-Dpioasm_DIR='+pioasm.as_posix(),
         '-DCMAKE_BUILD_TYPE=Release'],
        [str(cmake),'--build',str(build),'--parallel','8'],
    ]
    logs=[]
    for command in commands:
        result=subprocess.run(command,env=env,capture_output=True,text=True,
                              encoding='utf-8',errors='replace')
        logs.append(result.stdout+result.stderr)
        (output/'build.log').write_text('\n'.join(logs),encoding='utf-8')
        if result.returncode: raise SystemExit(logs[-1][-14000:])
    uf2=output/'RP2350_CM_HID_QUERY_PROBE_ONLY.uf2'
    elf=output/'RP2350_CM_HID_QUERY_PROBE_ONLY.elf'
    shutil.copyfile(build/'t3p_cm_host_probe.uf2',uf2)
    shutil.copyfile(build/'t3p_cm_host_probe.elf',elf)
    details=inspect_uf2(uf2)
    notices=output/'notices'
    notices.mkdir(exist_ok=True)
    for name,path in {
        'project-MIT.txt':source/'LICENSE',
        'pico-sdk-BSD-3-Clause.txt':ROOT/'local/pico-sdk/LICENSE.TXT',
        'tinyusb-MIT.txt':ROOT/'local/pico-sdk/lib/tinyusb/LICENSE',
        'Pico-PIO-USB-MIT.txt':ROOT/'local/Pico-PIO-USB/LICENSE',
    }.items(): shutil.copyfile(path,notices/name)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    manifest=dict(scope='Auxiliary RP2350-USB-CM only; never a T3P firmware',
        default_state='GPIO13/12 inputs, pulls disabled, PIO host not started',
        target_pins=dict(dp=13,dm=12,pinout='DMDP'),clock_khz=120000,
        native_cdc=dict(vid='0xcafe',pid='0x4001',product='T3P CM descriptor probe'),
        host_start='Explicit HOST_START command only',
        target_operations=['USB bus reset, standard enumeration, HID SET_IDLE and report descriptor',
                           'Explicit fixed 64-byte settings/measurement read requests after exact descriptor match'],
        absent_features=['arbitrary HID reports','settings writes','USB_KEY','SCSI',
                         'RAM loader','Flash read/erase/program','VBUS or CC control'],
        pio_usb_revision=PIO_REV,uf2=details,uf2_sha256=sha(uf2),elf_sha256=sha(elf),
        source_sha256={p.relative_to(ROOT).as_posix():sha(p)
                       for p in sorted(source.rglob('*')) if p.is_file()})
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__': main()
