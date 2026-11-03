"""Run actual C frame words against the assembled PIO instruction stream.

This models instruction cycles, not pad voltages, DMA arbitration or T3P ROM.
The scope test remains required. No hardware is enumerated or commanded.
"""
from pathlib import Path
import json
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    out=ROOT/'artifacts/cm-wave-scope-v4'
    clang=Path('D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe')
    exe=out/'plan-dump.exe'
    subprocess.run([str(clang),'-std=c11','-Wall','-Wextra','-Werror','-O2',
                    str(ROOT/'tests/cm_wave_plan_dump.c'),'-o',str(exe)],check=True)
    fifo=[int(s,16) for s in subprocess.check_output([str(exe)],text=True).splitlines()]
    header=(out/'build/key_wave.pio.h').read_text(encoding='utf-8')
    block=re.search(r't3p_scope_key_program_instructions\[\] = \{(.*?)\};',header,re.S).group(1)
    code=[int(x,16) for x in re.findall(r'^\s*(0x[0-9a-f]+),',block,re.M)]
    assert len(code)==9 and len(fifo)==48
    pc=x=y=osr=ticks=0; outputs=[]; release=None; consumed=0
    for _ in range(100000):
        instruction=code[pc]
        assert instruction & 0x1f00==0,'Unexpected instruction delay/side-set'
        op=instruction>>13; destination=(instruction>>5)&7; arg=instruction&31
        next_pc=0 if pc==8 else pc+1
        if op==4:
            assert instruction==0x80a0 and fifo,'Unexpected or starved PULL'
            osr=fifo.pop(0); consumed+=1
        elif op==3:
            count=arg or 32
            value=osr & ((1<<count)-1); osr>>=count
            if destination==0: assert count==2; outputs.append((ticks,value))
            elif destination==1: x=value
            elif destination==2: y=value
            else: raise AssertionError('Unexpected OUT destination')
        elif op==0:
            if destination==0: branch=True
            elif destination==3: branch=y==0
            elif destination==2:
                branch=x!=0; x=(x-1)&0xffffffff
            else: raise AssertionError('Unexpected JMP condition')
            if branch: next_pc=arg
        elif op==7:
            assert destination==4 and arg==0,'Output enables not released'
            release=ticks
        elif op==6:
            assert instruction==0xc000 and release is not None
            break
        else: raise AssertionError('Unexpected opcode')
        pc=next_pc; ticks+=1
    else: raise AssertionError('PIO did not finish')
    assert consumed==48 and not fifo and len(outputs)==48
    rises=[]; falls=[]; bits=[]; previous=0
    for at,pins in outputs:
        if pins&2 and not previous&2: rises.append(at); bits.append(pins&1)
        if previous&2 and not pins&2: falls.append(at)
        previous=pins
    assert len(rises)==len(falls)==16
    assert ''.join(map(str,bits))=='0001011011101111'
    assert all(f-r==1160 for r,f in zip(rises,falls))
    assert all(r-f==1155 for f,r in zip(falls,rises[1:]))
    assert all(rises[i]-outputs[i*3][0]==15 for i in range(1,16))
    assert release-falls[-1]==3
    assert rises[0]-outputs[0][0]==1155
    result=dict(scope='Offline C plan + assembled PIO cycle simulation only',
                word='0x16ef',bits=''.join(map(str,bits)),rising_edges=16,
                clock_gpio=13,data_gpio=12,clock_usb_line='D+',data_usb_line='D-',
                high_cycles=1160,low_cycles=1155,data_setup_cycles=15,
                release_after_last_fall_cycles=3,
                first_output_to_last_fall_cycles=falls[-1]-outputs[0][0],
                first_output_to_release_cycles=release-outputs[0][0],
                gpio_outputs_released_before_irq=True,all_48_words_consumed=True,
                limits='No hardware voltage/waveform measurement; DMA starvation checked separately on device')
    (ROOT/'analysis/cm-wave-timing-model.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
