"""Read CM scope-test status, or explicitly emit one fixed off-target frame.

T3P must be disconnected. Only the USB-C breakout and high-impedance scope
probes may be attached. No arbitrary waveform, target command or repeat API.
"""
import argparse
import json
from pathlib import Path
import time
from cm_host_probe import next_item


def operate(port,emit=False,bootsel=False):
    if emit and bootsel: raise ValueError('Choose only one action')
    initial=next_item(port,time.monotonic()+3)
    expected=('t3p_cm_wave_scope','RP2350-USB-CM','status',13,12,True,'down')
    keys=('program','board','event','dp_gpio','dm_gpio','bench_only','pulls')
    if tuple(initial.get(k) for k in keys)!=expected or initial.get('version') not in (1,2,3,4):
        raise ValueError('Unexpected scope-only firmware profile')
    released=initial.get('pio_oe')==0 and initial.get('fault') is False
    if emit and (not released or initial.get('clock_hz')!=120000000 or
                 (initial.get('dp'),initial.get('dm'))!=(0,0)):
        raise ValueError('Fault, enabled output, wrong clock or externally biased pins; emission refused')
    if emit and initial['version']>=3 and (initial.get('drive_2ma') is not True or initial.get('slew_slow') is not True):
        raise ValueError('GPIO drive/slew setting not verified')
    if emit and initial['version']==4 and (initial.get('key_clock_gpio'),initial.get('key_data_gpio'))!=(13,12):
        raise ValueError('BR35 candidate clock/data mapping not verified')
    command=b'SCOPE_KEY_ONCE\n' if emit else b'BOOTSEL\n' if bootsel else None
    if command and port.write(command)!=len(command): raise IOError('Short command write')
    result=dict(scope_requirement='CM plus scope only; T3P disconnected',initial=initial,emit_sent=emit)
    if emit:
        deadline=time.monotonic()+3
        while True:
            item=next_item(port,deadline)
            if item.get('event')=='wave_rejected': raise ValueError(str(item))
            if item.get('event')=='wave_finished': break
        result['finished']=item
        result['success']=(item.get('completed') is True and item.get('underrun') is False
                           and item.get('pio_oe')==0 and item.get('frames')==initial['frames']+1)
        if initial['version']>=2:
            result['success']=result['success'] and item.get('pio_oe_before_cpu_release')==0
    else:
        result['success']=True if bootsel else released
        result['bootsel_sent']=bootsel
    return result


def main():
    import serial
    from serial.tools import list_ports
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--output',required=True,type=Path)
    group=parser.add_mutually_exclusive_group()
    group.add_argument('--emit',action='store_true')
    group.add_argument('--bootsel',action='store_true')
    args=parser.parse_args()
    ports=[p for p in list_ports.comports() if (p.vid,p.pid)==(0x2e8a,0x0009)
           and (not args.port or p.device.upper()==args.port.upper())]
    if len(ports)!=1: raise SystemExit('Need one expected CM scope-test CDC port')
    with serial.Serial(ports[0].device,115200,timeout=0.1,write_timeout=1) as port:
        result=operate(port,args.emit,args.bootsel)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))
    return 0 if result['success'] else 1


if __name__=='__main__': raise SystemExit(main())
