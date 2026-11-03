"""Exact CM power-edge helper; default is status only, with no output signals.

ADC checks and bias checks require T3P physically disconnected. The divider
must be measured before connecting GPIO26. Failed commands are never retried.
Optional powered-off wait renewal stops permanently on power activity.
"""
import argparse
import json
from pathlib import Path
import time
from cm_host_probe import next_item
from cm_wait_lease import WaitLease

ACTIONS={
    'adc-on':b'ADC_ON_CHECK\n','adc-off':b'ADC_OFF_CHECK\n',
    'bias':b'BIAS_CHECK\n','bench':b'BENCH_FRAME_AND_HOST\n',
    'arm':b'ARM_STABLE_POWER_ONCE\n','check-arm':b'ARM_STABLE_POWER_ONCE\n',
    'power-bench':b'ARM_STABLE_POWER_ONCE\n',
    'host':b'HOST_START\n','bootsel':b'BOOTSEL\n',
    'delay-100':b'DELAY_100\n','delay-1000':b'DELAY_1000\n','delay-5000':b'DELAY_5000\n',
}
FAULTS={'host_init_failed','line_held_low_timeout','bias_release_failed',
        'power_dropped','power_unstable_timeout','power_lost_before_key','power_lost_after_key'}

def identify(item):
    expected=('t3p_cm_power_probe','RP2350-USB-CM','status',13,12,120000000,10,26,0)
    keys=('program','board','event','key_clock_gpio','key_data_gpio',
          'clock_hz','bias_gpio','adc_gpio','adc_channel')
    if tuple(item.get(k) for k in keys)!=expected or type(item.get('version')) is not int or item['version'] not in (1,2,3):
        raise ValueError('Wrong CM power-edge firmware profile')
    if type(item.get('key_delay_us')) is not int or item['key_delay_us'] not in ((100,1000,5000) if item['version']==3 else (100,)):
        raise ValueError('Wrong key delay')
    if item['version']>=2 and (item.get('power_policy')!='stable_before_first_key' or item.get('power_settle_limit_us')!=50000):
        raise ValueError('Wrong stable-power policy')
    for key in ('armed','used','host_requested','fault','bias_active','bias_checked',
                'adc_on_checked','adc_off_checked'):
        if type(item.get(key)) is not bool: raise ValueError('Invalid state fields')
    if type(item.get('adc_raw')) is not int or not 0<=item['adc_raw']<=4095:
        raise ValueError('Invalid ADC reading')
    return item

def send(port,raw):
    if port.write(raw)!=len(raw): raise IOError('Short command write; never retry')

def operate(port,output,action=None,seconds=5,keep_waiting=False):
    if action is not None and action not in ACTIONS: raise ValueError('Action not permitted')
    if keep_waiting and action not in ('arm','power-bench'): raise ValueError('Wait renewal is for power arm only')
    first=identify(next_item(port,time.monotonic()+3))
    result=dict(initial=first,events=[],action=action,success=action is None,
                scope='CM helper only; single fixed frame and standard USB descriptors; no class/SCSI/flash')
    last_status_signature=None
    last_status_write=None
    def record(item):
        nonlocal last_status_signature,last_status_write
        if keep_waiting and item.get('event')=='status':
            signature=tuple(item.get(k) for k in ('armed','used','host_requested','fault','frames',
                           'bias_active','bias_checked','adc_on_checked','adc_off_checked'))+(item.get('adc_raw',4095)>310,)
            now=time.monotonic()
            if signature==last_status_signature and last_status_write is not None and now-last_status_write<10:
                return  # Still process every status; persist unchanged wait status at most once/10s.
            last_status_signature=signature;last_status_write=now
        result['events'].append(item)
        output.parent.mkdir(parents=True,exist_ok=True)
        output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(item),flush=True)
    record(first)
    if action:
        if action!='bootsel':
            if action in ('arm','check-arm','power-bench') and first['version']<2:
                raise ValueError('Stable power-edge tests require helper v2 or v3')
            if action.startswith('delay-') and first['version']!=3:
                raise ValueError('Delay selection requires helper v3')
            if any(first[k] for k in ('armed','used','host_requested','fault','bias_active')) or first.get('frames')!=0:
                raise ValueError('Needs unused, inactive, fault-free helper')
            if action in ('adc-off','bias','bench','arm','check-arm','power-bench','host') and not first['adc_on_checked']:
                raise ValueError('ADC ON check required first')
            if action in ('bench','arm','check-arm','power-bench','host') and not first['adc_off_checked']:
                raise ValueError('ADC OFF check required first')
            if action in ('bench','arm','check-arm','power-bench') and not first['bias_checked']:
                raise ValueError('External bias check required first')
            on=action in ('adc-on','bias','bench','host')
            if on and not 2483<=first['adc_raw']<=3352:
                raise ValueError('Target VBUS must be ON for this action')
            if action in ('adc-off','arm','check-arm','power-bench') and first['adc_raw']>310:
                raise ValueError('Target VBUS must be OFF for this action')
        send(port,ACTIONS[action])
        if action=='bootsel':
            result['success']=True
            output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
            return result
    deadline=time.monotonic()+seconds
    disarmed=False
    lease=WaitLease() if keep_waiting else None
    result['wait_renewals']=0
    while time.monotonic()<deadline:
        try: item=next_item(port,lease.read_deadline(deadline) if lease else deadline)
        except TimeoutError:
            if lease and lease.pending_deadline is not None:
                raise TimeoutError('Wait renewal was not acknowledged; no retry')
            break
        record(item)
        ev=item.get('event')
        if ev in ('unknown_or_repeated_command','arm_rejected','bench_rejected','bias_check_rejected','line_too_long'):
            raise ValueError('Helper rejected action; no retry')
        if ev in FAULTS: break
        if ev=='delay_set' and action and action.startswith('delay-'):
            result['success']=(item.get('key_delay_us')==int(action.split('-')[1]) and
                               item.get('signal_sent') is False)
            break
        if ev=='status':
            identify(item)
            if item['fault']: break
            if disarmed:
                result['success']=not any(item[k] for k in ('armed','used','host_requested','fault','bias_active')) and item.get('frames')==0
                break
        if lease:
            if time.monotonic()>deadline-10 and lease.pending_deadline is None: lease.stop()
            renewal=lease.feed(item,time.monotonic())
            result['wait_renewals']=lease.renewals
            if renewal:
                send(port,renewal)
                record(dict(event='wait_renewal_action',source='controller',command=renewal.decode().strip()))
        if ev=='adc_check_finished' and action in ('adc-on','adc-off'):
            on=action=='adc-on'
            result['success']=(item.get('on') is on and item.get('passed') is True and
                               item.get('adc_on_checked') is True and
                               (on or item.get('adc_off_checked') is True))
            break
        if ev=='bias_check_finished' and action=='bias':
            result['success']=all(item.get(k) is True for k in ('passed','high_seen','low_seen','bias_released'))
            break
        if ev=='armed' and action=='check-arm':
            send(port,b'DISARM\n'); disarmed=True
        if ev=='key_finished' and (item.get('fault') is not False or item.get('frames')!=1): break
        if ev=='host_started' and action in ('bench','power-bench'):
            keys=[e for e in result['events'] if e.get('event')=='key_finished']
            result['success']=(len(keys)==1 and keys[0].get('completed') is True and
                keys[0].get('underrun') is False and keys[0].get('pio_oe_before_cpu_release')==0 and
                keys[0].get('fault') is False and keys[0].get('frames')==1 and any(
                    e.get('event')=='biased_line_observation' and e.get('bias_released') is True and
                    e.get('low_pulse_candidate') is False for e in result['events']))
            if action=='power-bench':
                edges=[e for e in result['events'] if e.get('event')=='power_edge']
                timing=[e for e in result['events'] if e.get('event')=='key_start_timing']
                result['success']=result['success'] and len(edges)==len(timing)==1 and (
                    timing[0].get('edge_detected') is True and
                    type(timing[0].get('actual_enable_delay_us')) is int and
                    timing[0]['actual_enable_delay_us']>=first['key_delay_us'])
            break
        if ev=='device_mounted' and action in ('arm','host'):
            keys=[e for e in result['events'] if e.get('event')=='key_finished']
            result['success']=action=='host' or (len(keys)==1 and keys[0].get('completed') is True and
                keys[0].get('underrun') is False and keys[0].get('pio_oe_before_cpu_release')==0 and
                keys[0].get('fault') is False and keys[0].get('frames')==1)
            break
        if ev=='device_mounted' and action is None:
            # Read-only observer for an already running descriptor host.
            # No command is sent, and a fresh mount ends the bounded reader.
            result['success']=True
            break
        if ev=='arm_expired': break
    result['controller_fault']=any(e.get('event') in FAULTS or e.get('fault') is True for e in result['events'])
    if result['controller_fault']: result['success']=False
    result['rom_identity_seen']=any(e.get('event')=='device_mounted' and e.get('rom_identity_match') is True for e in result['events'])
    result['stock_identity_seen']=any(e.get('event')=='device_mounted' and e.get('stock_identity_match') is True for e in result['events'])
    result['target_flash_access']=False
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result

def main():
    import serial
    from serial.tools import list_ports
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--action',choices=tuple(ACTIONS))
    p.add_argument('--seconds',type=float,default=5)
    p.add_argument('--keep-waiting',action='store_true',help='Renew only a still-unused OFF wait, never retry a key')
    a=p.parse_args()
    if not 2<=a.seconds<=3615: p.error('Use 2..3615 seconds')
    if a.keep_waiting and a.action not in ('arm','power-bench'): p.error('Use --keep-waiting only with arm or power-bench')
    if a.seconds>615 and a.action in ('arm','power-bench') and not a.keep_waiting:
        p.error('Power waits longer than615s require --keep-waiting')
    ports=[x for x in list_ports.comports() if (x.vid,x.pid)==(0xcafe,0x4001)]
    if len(ports)!=1: raise SystemExit('Need exactly one CM CDC candidate')
    with serial.Serial(ports[0].device,115200,timeout=.1,write_timeout=1) as port:
        result=operate(port,a.output,a.action,a.seconds,a.keep_waiting)
    print(json.dumps({k:v for k,v in result.items() if k!='events'}),flush=True)
    return 0 if result.get('success') else 1

if __name__=='__main__': raise SystemExit(main())
