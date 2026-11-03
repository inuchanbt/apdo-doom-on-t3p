"""Control only the exact auxiliary CM one-attach ROM descriptor probe.

Default reads status. --check-arm arms and immediately disarms with T3P absent.
--arm waits for one physical attach and one frame; it NEVER retries.
Closing the CDC connection cancels an untriggered arm in firmware.
"""
import argparse
import json
from pathlib import Path
import time
from cm_host_probe import next_item


def identify(item):
    expected=('t3p_cm_rom_probe','RP2350-USB-CM','status',13,12,120000000)
    keys=('program','board','event','key_clock_gpio','key_data_gpio','clock_hz')
    if tuple(item.get(k) for k in keys)!=expected or item.get('version') not in (1,2,3):
        raise ValueError('Wrong CM firmware or key mapping')
    for key in ('armed','used','host_requested','fault'):
        if type(item.get(key)) is not bool: raise ValueError('Invalid state fields')
    if item['version']==3 and (item.get('bias_gpio')!=10 or
           any(type(item.get(k)) is not bool for k in ('bias_active','bias_checked'))):
        raise ValueError('Wrong external-bias profile')
    return item


def command(port,raw):
    if port.write(raw)!=len(raw): raise IOError('Short command write; never retry')


def operate(port,output,arm=False,check_arm=False,bootsel=False,seconds=5,bench=False,bias_check=False):
    if sum(map(bool,(arm,check_arm,bootsel,bench,bias_check)))>1: raise ValueError('One action only')
    first=identify(next_item(port,time.monotonic()+3))
    result=dict(initial=first,events=[],arm_sent=False,
                scope='One fixed key frame then standard descriptors only; no class requests or SCSI')
    def record(item):
        result['events'].append(item)
        output.parent.mkdir(parents=True,exist_ok=True)
        output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(item),flush=True)
    record(first)
    if bootsel:
        command(port,b'BOOTSEL\n'); return result
    if arm or check_arm or bench or bias_check:
        if any(first[k] for k in ('armed','used','host_requested','fault')) or first.get('frames')!=0:
            raise ValueError('Needs unused, unarmed, fault-free, host-not-started CM')
        if first['version']==3:
            if first['bias_active']: raise ValueError('External bias unexpectedly active')
            if not bias_check and not first['bias_checked']: raise ValueError('External resistor path not checked')
        if bias_check and first['version']!=3: raise ValueError('Bias check requires CM probe v3')
        command(port,b'BIAS_CHECK\n' if bias_check else b'BENCH_FRAME_AND_HOST\n' if bench else b'ARM_ATTACH_ONCE\n')
        result['arm_sent']=not (bench or bias_check)
        result['bench_sent']=bench
    deadline=time.monotonic()+seconds
    disarm_sent=False
    while time.monotonic()<deadline:
        try: item=next_item(port,deadline)
        except TimeoutError: break
        record(item)
        if item.get('event')=='arm_rejected': raise ValueError('CM refused arming baseline')
        if item.get('event')=='bench_rejected': raise ValueError('CM refused scope-only baseline')
        if item.get('event')=='bias_check_rejected': raise ValueError('CM refused disconnected bias check')
        if item.get('event')=='bias_check_finished' and bias_check:
            result['bias_check_pass']=all(item.get(k) is True for k in ('passed','high_seen','low_seen','bias_released'))
            if not result['bias_check_pass']: raise ValueError('External resistor path verification failed')
            break
        if item.get('event')=='armed' and check_arm:
            command(port,b'DISARM\n'); disarm_sent=True
        if disarm_sent and item.get('event')=='status' and item.get('armed') is False:
            if item.get('frames')!=0 or item.get('used') or item.get('host_requested'):
                raise ValueError('Unexpected transmission during arm/disarm check')
            result['check_arm_pass']=True; break
        if item.get('event') in ('device_mounted','arm_expired','host_init_failed',
                                  'line_held_low_timeout','bias_release_failed'): break
        if bench and item.get('event')=='host_started': break
        if item.get('event')=='key_finished' and (item.get('fault') or item.get('frames')!=1):
            raise ValueError('Key completion fault; no retries')
    result['rom_identity_seen']=any(e.get('rom_identity_match') is True for e in result['events'])
    result['target_flash_access']=False
    result['controller_fault']=any(e.get('event') in ('host_init_failed','line_held_low_timeout','bias_release_failed')
                                   for e in result['events'])
    if bench:
        keys=[e for e in result['events'] if e.get('event')=='key_finished']
        result['bench_pass']=(len(keys)==1 and keys[0].get('completed') is True
                              and keys[0].get('underrun') is False
                              and keys[0].get('pio_oe_before_cpu_release')==0
                              and keys[0].get('frames')==1 and keys[0].get('fault') is False
                              and any(e.get('event')=='host_started' for e in result['events']))
        if first['version']==3:
            result['bench_pass']=result['bench_pass'] and any(
                e.get('event')=='biased_line_observation' and e.get('bias_released') is True
                and e.get('low_pulse_candidate') is False for e in result['events'])
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result


def main():
    import serial
    from serial.tools import list_ports
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    g=p.add_mutually_exclusive_group()
    g.add_argument('--arm',action='store_true'); g.add_argument('--check-arm',action='store_true')
    g.add_argument('--bootsel',action='store_true')
    g.add_argument('--bench',action='store_true',help='T3P physically disconnected: one frame then host')
    g.add_argument('--bias-check',action='store_true',help='T3P disconnected: verify GPIO10 through 5k resistor to D+')
    p.add_argument('--seconds',type=float,default=5)
    a=p.parse_args()
    if not 2<=a.seconds<=615: p.error('Use 2..615 seconds')
    candidates=[x for x in list_ports.comports() if (x.vid,x.pid)==(0xcafe,0x4001)]
    if len(candidates)!=1: raise SystemExit('Need one CM CDC candidate')
    with serial.Serial(candidates[0].device,115200,timeout=.1,write_timeout=1) as port:
        result=operate(port,a.output,a.arm,a.check_arm,a.bootsel,a.seconds,a.bench,a.bias_check)
    print(json.dumps({k:v for k,v in result.items() if k!='events'}),flush=True)
    if a.check_arm and not result.get('check_arm_pass'): return 1
    if a.bench and not result.get('bench_pass'): return 1
    if a.bias_check and not result.get('bias_check_pass'): return 1
    if result.get('controller_fault'): return 1
    return 0


if __name__=='__main__': raise SystemExit(main())
