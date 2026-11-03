"""Build a one-frame, scope-only USB_KEY candidate; never install or emit."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from build_cm_input_check import inspect_uf2

ROOT=Path(__file__).resolve().parents[1]


def main():
    tools=ROOT/'local/tools'
    compiler=tools/'arm-gcc/bin/arm-none-eabi-gcc.exe'
    cmake=tools/'pico-build/cmake/data/bin/cmake.exe'
    ninja=tools/'pico-build/ninja-1.11.1.3.data/scripts/ninja.exe'
    source=ROOT/'recovery/cm_wave_scope'
    output=ROOT/'artifacts/cm-wave-scope-v4'
    build=output/'build'
    output.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy(); env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    commands=[
        [str(cmake),'-S',str(source),'-B',str(build),'-G','Ninja',
         '-DCMAKE_MAKE_PROGRAM='+ninja.as_posix(),
         '-DPICO_TOOLCHAIN_PATH='+compiler.parent.as_posix(),
         '-DPython3_EXECUTABLE=D:/KM003C/.venv/Scripts/python.exe',
         '-Dpicotool_DIR='+(tools/'pico-build/picotool/picotool').as_posix(),
         '-Dpioasm_DIR='+(tools/'pico-build/sdk-tools/pioasm').as_posix(),
         '-DCMAKE_BUILD_TYPE=Release'],
        [str(cmake),'--build',str(build),'--parallel','8'],
    ]
    logs=[]
    for command in commands:
        done=subprocess.run(command,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace')
        logs.append(done.stdout+done.stderr)
        (output/'build.log').write_text('\n'.join(logs),encoding='utf-8')
        if done.returncode: raise SystemExit(logs[-1][-12000:])
    uf2=output/'RP2350_CM_WAVE_SCOPE_BENCH_ONLY.uf2'
    elf=output/'RP2350_CM_WAVE_SCOPE_BENCH_ONLY.elf'
    shutil.copyfile(build/'t3p_cm_wave_scope.uf2',uf2)
    shutil.copyfile(build/'t3p_cm_wave_scope.elf',elf)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    details=inspect_uf2(uf2)
    notices=output/'notices'; notices.mkdir(exist_ok=True)
    for name,path in {'project-MIT.txt':source/'LICENSE',
        'pico-sdk-BSD-3-Clause.txt':ROOT/'local/pico-sdk/LICENSE.TXT',
        'tinyusb-MIT.txt':ROOT/'local/pico-sdk/lib/tinyusb/LICENSE'}.items(): shutil.copyfile(path,notices/name)
    metadata=dict(scope='CM + scope only; T3P must be disconnected',
        default_state='GPIO13/12 inputs with weak pulldowns; no automatic transmission',
        command='SCOPE_KEY_ONCE: one fixed frame, no repeat',word='0x16ef',bits='0001011011101111',
        gpio=dict(clock_dp=13,data_dm=12,drive_strength_ma_setting=2,slew='slow'),clock_hz=120000000,
        nominal_cycles=dict(high=1160,low=1155,data_setup=15,release_after_last_fall=3),
        nominal_microseconds=dict(high=1160/120,low=1155/120,data_setup=15/120,
                                  hardware_release_after_last_fall=3/120),
        hardware_guards=['Refuse if either input high under weak pulldown','20ms CPU timeout',
                         'PIO releases both output enables before IRQ','FIFO starvation flag checked',
                         'Output enables read before CPU fallback release',
                         'CPU redundantly releases both output enables','Any error latches transmission fault'],
        absent_features=['USB host','target USB commands','ACK detection','ROM entry verification',
                         'RAM loader','Flash read/erase/program','VBUS/CC control'],
        warning='Timing candidate based on reference research; actual waveform and T3P timing not verified',
        uf2=details,uf2_sha256=sha(uf2),elf_sha256=sha(elf),
        source_sha256={p.relative_to(ROOT).as_posix():sha(p) for p in sorted(source.rglob('*')) if p.is_file()})
    (output/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(metadata,indent=2))


if __name__=='__main__': main()
