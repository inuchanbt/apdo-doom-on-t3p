"""Commands for the exact auxiliary CM diagnostic, never for a T3P.

Read and validate the running diagnostic before sending one of two fixed
commands. PULLCHECK requires the CM male port empty and checks only weak input
pulls; BOOTSEL resets this CM itself. No arbitrary command or USB traffic API.
"""
import argparse
import json
from pathlib import Path
import time

PROFILE=('inputs_only','RP2350-USB-CM',13,12)
PHASES=[('down',(0,0)),('up',(1,1)),('dp_down_dm_up',(0,1)),
        ('dp_up_dm_down',(1,0)),('disabled',None)]


def validate_sample(item):
    if (item.get('mode'),item.get('board'),item.get('dp_gpio'),item.get('dm_gpio'))!=PROFILE:
        raise ValueError('Unexpected diagnostic board or pin profile')
    if item.get('version') not in (1,2):
        raise ValueError('Unknown diagnostic version')
    if any(type(item.get(k)) is not int or item[k] not in (0,1) for k in ('dp','dm')):
        raise ValueError('Invalid GPIO readings')
    return item


def next_sample(port,deadline):
    while time.monotonic()<deadline:
        line=port.readline(1025)
        if len(line)>1024:
            raise ValueError('Serial line too long')
        try:
            item=json.loads(line)
        except (ValueError,UnicodeDecodeError):
            continue
        if isinstance(item,dict) and item.get('program')=='t3p_cm_input_check':
            return validate_sample(item)
    raise TimeoutError('No valid CM diagnostic response')


def run_command(port,command,count=3):
    # Opening at 115200 and reading first cannot trigger the disabled
    # 1200-baud/vendor reset; identity must arrive from our diagnostic itself.
    initial=next_sample(port,time.monotonic()+3)
    if command=='bootsel':
        if port.write(b'BOOTSEL\n')!=8:
            raise IOError('Short BOOTSEL write')
        return dict(scope='Auxiliary CM only',command='BOOTSEL',initial=initial,
                    status='Sent; verify BOOTSEL enumeration separately')
    if command!='pull-check':
        raise ValueError('Command outside whitelist')
    if not 1<=count<=10:
        raise ValueError('Use 1..10 cycles')
    if initial['version']!=2 or initial.get('pulls')!='disabled':
        raise ValueError('PULLCHECK requires released diagnostic version 2')
    cycles=[]
    for _ in range(count):
        if port.write(b'PULLCHECK\n')!=10:
            raise IOError('Short PULLCHECK write')
        readings=[]
        deadline=time.monotonic()+3
        for phase,expected in PHASES:
            sample=next_sample(port,deadline)
            # A previously queued periodic released sample can precede down.
            while not readings and sample.get('pulls')=='disabled':
                sample=next_sample(port,deadline)
            if sample.get('pulls')!=phase:
                raise ValueError('Missing or out-of-order weak-pull phase')
            readings.append(sample)
        passed=all(expected is None or (sample['dp'],sample['dm'])==expected
                   for sample,(_,expected) in zip(readings,PHASES))
        cycles.append(dict(passed=passed,readings=readings))
        if not passed:
            break
    return dict(scope='Auxiliary CM only; male port must be empty',
                command='PULLCHECK',initial=initial,cycles=cycles,
                success=len(cycles)==count and all(x['passed'] for x in cycles),
                limits='Digital weak-pull check only; not USB host, analog voltage, USB_KEY or T3P recovery verification')


def main():
    import serial
    from serial.tools import list_ports
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['bootsel','pull-check'])
    parser.add_argument('--port',required=True)
    parser.add_argument('--count',type=int,choices=range(1,11),default=3)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    selected=[p for p in list_ports.comports() if p.device.upper()==args.port.upper()]
    if len(selected)!=1 or (selected[0].vid,selected[0].pid)!=(0x2e8a,0x0009):
        raise SystemExit('Port must identify exactly one expected RP2350 CDC diagnostic')
    with serial.Serial(selected[0].device,115200,timeout=0.1,write_timeout=1) as port:
        result=run_command(port,args.command,args.count)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))
    return 0 if result.get('success',True) else 1


if __name__=='__main__': raise SystemExit(main())
