"""Inspect pinned official SDK driver members locally, never run SDK postbuild.

These SDK inputs are research-only, not project release/distribution assets.
No firmware image is generated; ROM imports are NOT bound to reference stubs.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request
from build_engine import section_sizes

ROOT=Path(__file__).resolve().parents[1]
REV='d0167685d032d745d88fe50233302edd46941622'
BASE='https://gitlab.zh-jieli.com/e_badge/e_badge_707_sdk_200/-/raw/'+REV+'/'
ARCHIVES={
    'cpu.a':'787b6fc0913a0a8a634ee8c7606d5e4a68dc5604d8feacf806fd60dc1e2fc8ca',
    'common_lib.a':'370cc5dd13e6b636f61c5122302dc1dd3be01ffa9a2dcef256d8b5e3091b8dcd',
    'device.a':'e6cad5ce44970000c1631275f6b2175a7c68a0f191a2c66af8dee4c0d9f8fb3e',
}


def sha(data):return hashlib.sha256(data).hexdigest()


def main():
    folder=ROOT/'local/jieli-sdk/SDK/cpu/br35/liba'
    out=ROOT/'local/sdk-runtime-research'
    folder.mkdir(parents=True,exist_ok=True);out.mkdir(exist_ok=True)
    archives=[]
    for name,expected in ARCHIVES.items():
        p=folder/name;url=BASE+'SDK/cpu/br35/liba/'+name
        data=p.read_bytes() if p.exists() else urllib.request.urlopen(url,timeout=45).read(16000000)
        if not data.startswith(b'!<arch>\n') or sha(data)!=expected:
            raise ValueError('Pinned SDK archive mismatch: '+name)
        if not p.exists():p.write_bytes(data)
        archives.append(dict(path=p.relative_to(ROOT).as_posix(),url=url,bytes=len(data),sha256=expected))
    tools=ROOT/'local/tools/jieli/C$/JL/pi32/bin'
    members=[]
    for name,stem in [('startup.S.o','startup'),('clock.c.o','clock'),('spi_hw_v2.c.o','spi'),
                      ('gpio_hw.c.o','gpio_hw'),('wdt_p33.c.o','wdt_p33'),('timer.c.o','timer')]:
        src=out/name
        data=subprocess.run([str(tools/'llvm-ar.exe'),'p',str(folder/'cpu.a'),name],
                            capture_output=True,check=True).stdout
        src.write_bytes(data)
        native=src
        if not data.startswith(b'\x7fELF'):
            native=out/(stem+'-native.o')
            subprocess.run([str(tools/'clang.exe'),'-target','pi32v2','-mcpu=r3','-integrated-as',
                            '-mllvm','-pi32v2-large-program=true','-O2','-c','-x','ir',str(src),
                            '-o',str(native)],check=True)
        disasm=subprocess.run([str(tools/'llvm-objdump.exe'),'-d','-r','-print-imm-hex',str(native)],
                              capture_output=True,check=True).stdout
        (out/(stem+'-native.disasm.txt')).write_bytes(disasm)
        imports=subprocess.run([str(tools/'llvm-nm.exe'),'--undefined-only',str(native)],
                               capture_output=True,text=True,check=True).stdout
        members.append(dict(member=name,sha256=sha(data),native_sha256=sha(native.read_bytes()),
                            unlinked_section_totals=section_sizes(native),
                            imports=[line.split()[-1] for line in imports.splitlines() if line.split()]))
    report=dict(status='OFFLINE SDK driver inspection only; no ROM import binding or firmware',
                revision=REV,distribution='Local research only; no blanket SDK license assumed',
                archives=archives,members=members,
                not_verified=['T3P ROM table ABI','board-specific pinmux/clock/power settings',
                              'independent LCD initialization','runtime boot handoff'])
    (ROOT/'analysis/sdk-runtime-drivers.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Verified3 pinned archives, compiled/inspected6 driver/startup members; no USB or firmware writes')


if __name__=='__main__':main()
