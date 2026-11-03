"""Check CPU/memory layout and reject RAM/Flash overflow, entirely offline."""
import json
from pathlib import Path
import subprocess
from runtime_layout_evidence import inspect_layout

ROOT=Path(__file__).resolve().parents[1]


def main():
    tools=ROOT/'local/tools/jieli/C$/JL/pi32/bin'
    folder=ROOT/'artifacts/pi32v2-engine/runtime-layout'
    report=inspect_layout(folder/'DO-NOT-FLASH-runtime-layout.elf')
    scratch=ROOT/'local/runtime-layout-check'
    scratch.mkdir(exist_ok=True)
    # These sections are explicitly retained, so garbage collection cannot
    # make the deliberately oversized input disappear and hide an overflow.
    checks=[]
    for name,section,flags,size in [('RAM','.bss.extra','aw',0x40000),
                                     ('Flash','.rodata.extra','a',0x200000)]:
        src=scratch/(name+'.s');obj=scratch/(name+'.o')
        src.write_text(f'.section {section},"{flags}",@'+('nobits' if name=='RAM' else 'progbits')+
                       f'\n.global t3p_layout_overflow\nt3p_layout_overflow:\n.space {size}\n',encoding='ascii')
        subprocess.run([str(tools/'clang.exe'),'-target','pi32v2','-mcpu=r3','-integrated-as',
                        '-c',str(src),'-o',str(obj)],check=True)
        lib=tools.parent/'pi32v2-lib/r3-large'
        cmd=[str(tools/'pi32v2-ld.exe'),'-T',str(ROOT/'native/runtime_layout.ld'),
             '--gc-sections','--undefined=t3p_layout_overflow','-o',str(scratch/(name+'.elf')),
             str(folder/'linkcheck_platform.o'),str(folder/'game_pack.o'),
             str(ROOT/'artifacts/pi32v2-engine/libt3pdoom.a'),
             str(folder/'runtime_start.S.o'),str(folder/'runtime_memory.c.o'),str(obj),
             str(folder/'t3p_boot.c.o'),
             str(folder/'t3p_signal.c.o'),
             '--start-group',str(lib/'libcompiler-rt.a'),str(lib/'libm.a'),str(lib/'libc.a'),'--end-group']
        result=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',errors='replace')
        log=result.stdout+result.stderr
        (scratch/(name+'.log')).write_text(log,encoding='utf-8')
        assert result.returncode!=0 and ('overflow' in log.lower() or 'overlap' in log.lower()
                                        or 'budget exceeded' in log.lower()),log
        checks.append(dict(region=name,extra_bytes=size,rejected=True))
    host=ROOT/'artifacts/host-check/runtime-memory.exe'
    subprocess.run(['D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe',
                    '-std=c11','-O2','-Wall','-Wextra','-Werror','-DT3P_RUNTIME_MEMORY_HOST',
                    str(ROOT/'native/runtime_memory.c'),str(ROOT/'tests/runtime_memory.c'),
                    '-o',str(host)],check=True)
    subprocess.run([str(host)],check=True)
    report['negative_link_checks']=checks
    (folder/'checks.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS: runtime entry/layout, aligned initialized data, ROM/update reservations, RAM/Flash overflow rejection')


if __name__=='__main__':main()
