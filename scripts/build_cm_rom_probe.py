"""Offline build of the one-attach ROM descriptor probe; never flashes or emits."""
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
    source=ROOT/'recovery/cm_rom_probe'
    output=ROOT/'artifacts/cm-rom-probe-v3'
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
    uf2=output/'RP2350_CM_ONE_ATTACH_ROM_PROBE.uf2'
    elf=output/'RP2350_CM_ONE_ATTACH_ROM_PROBE.elf'
    shutil.copyfile(build/'t3p_cm_rom_probe.uf2',uf2)
    shutil.copyfile(build/'t3p_cm_rom_probe.elf',elf)
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
    manifest=dict(scope='Auxiliary CM: explicit one-attach key, then standard USB descriptors only',
        idle_state='GPIO10/12/13 inputs without pulls, no automatic host or waveform',
        key=dict(clock_dp_gpio=13,data_dm_gpio=12,word='0x16ef',frames_per_boot=1),
        arm='Explicit ARM_ATTACH_ONCE, disconnected high/high baseline under weak pullups, both low for 100us, expires 600s, cancels on native CDC disconnect',
        bias='GPIO10 -> two parallel 10k resistors (5k) -> D+. BIAS_CHECK required before arming. GPIO10 inputs except explicit check/post-key observation.',
        handoff='Hardware OE release, enable 5k bias, free waveform PIO/DMA, observe low >=500us and release high >=20us (max8ms); release bias before host. Held-low timeout prevents host.',
        absent_features=['Automatic retries','ACK assertion from weak pulls','class requests','MSC/SCSI','RAM loader','Flash access','VBUS switching'],
        validation='Build and offline tests only until separate physical evidence is recorded',
        pio_usb_revision=PIO_REV,uf2=details,uf2_sha256=sha(uf2),elf_sha256=sha(elf),
        source_sha256={p.relative_to(ROOT).as_posix():sha(p)
                       for p in sorted(source.rglob('*')) if p.is_file()},
        shared_wave_sha256={n:sha(ROOT/'recovery/cm_wave_scope'/n) for n in ('key_wave.pio','wave_plan.h')})
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__': main()
