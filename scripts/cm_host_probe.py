"""Operate only the auxiliary CM descriptor probe's native CDC port.

Without an action flag, reads local status only. --start begins normal USB/HID
enumeration. Version 2 --query sends only fixed settings/measurement read
requests; no ROM/update operation exists. --bootsel resets the CM itself.
Requires the exact VID/PID and running probe profile.
"""
import argparse
import json
from pathlib import Path
import time


def next_item(port,deadline):
    while time.monotonic()<deadline:
        line=port.readline(1025)
        if len(line)>1024: raise ValueError('Serial line too long')
        try: value=json.loads(line)
        except (ValueError,UnicodeDecodeError): continue
        if isinstance(value,dict): return value
    raise TimeoutError('No descriptor probe response')


def identify(port):
    item=next_item(port,time.monotonic()+3)
    expected=('t3p_cm_host_probe','RP2350-USB-CM','status',13,12)
    actual=tuple(item.get(k) for k in ('program','board','event','dp_gpio','dm_gpio'))
    if actual!=expected or item.get('version') not in (1,2) or type(item.get('host_requested')) is not bool:
        raise ValueError('Unexpected CM descriptor probe profile')
    return item


def run_probe(port,start=False,bootsel=False,seconds=10,query=None):
    if sum([bool(start),bool(bootsel),query is not None])>1: raise ValueError('Choose only one action')
    if query not in (None,'settings','measurement'): raise ValueError('Query outside whitelist')
    if not 2<=seconds<=30: raise ValueError('Use 2..30 seconds')
    initial=identify(port)
    if start and initial['host_requested']:
        raise ValueError('Host already started; refusing repeated command')
    if query and (initial['version']!=2 or not initial['host_requested']):
        raise ValueError('Query requires running host probe version 2')
    command=b'HOST_START\n' if start else b'BOOTSEL\n' if bootsel else None
    if query: command=b'Q_SETTINGS\n' if query=='settings' else b'Q_MEASUREMENT\n'
    if command and port.write(command)!=len(command): raise IOError('Short command write')
    if bootsel:
        return dict(scope='Auxiliary CM only',command='BOOTSEL',initial=initial,
                    status='Sent; verify boot enumeration separately')
    events=[initial]
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        try: item=next_item(port,deadline)
        except TimeoutError: break
        events.append(item)
    matches=[e for e in events if e.get('event')=='hid_descriptor'
             and (e.get('vid'),e.get('pid'))==(0x0483,0x5750)
             and e.get('stock_t3p_match') is True]
    replies=[e for e in events if e.get('event')=='query_reply' and e.get('kind')==query]
    if query:
        from t3p_queries import describe
        for reply in replies:
            parsed=describe(bytes.fromhex(reply['hex']))
            reply['parsed']=parsed
            reply['valid']=reply.get('valid') is True and parsed['type']==query
    return dict(scope='CM native CDC and explicitly started PIO host',query=query,
                host_start_sent=start,events=events,
                stock_t3p_descriptor_seen=bool(matches),
                success=(len(replies)==1 and replies[0]['valid']) if query else bool(matches) if start else True,
                limits='Standard enumeration and two fixed stock read queries only; no setting changes, USB_KEY, SCSI or Flash access')


def main():
    import serial
    from serial.tools import list_ports
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list',action='store_true')
    parser.add_argument('--port')
    actions=parser.add_mutually_exclusive_group()
    actions.add_argument('--start',action='store_true')
    actions.add_argument('--bootsel',action='store_true')
    actions.add_argument('--query',choices=['settings','measurement'])
    parser.add_argument('--seconds',type=float,default=10)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    candidates=[p for p in list_ports.comports() if (p.vid,p.pid)==(0xcafe,0x4001)]
    if args.list:
        print(json.dumps([dict(port=p.device,description=p.description) for p in candidates],indent=2))
        return 0
    if not args.output: parser.error('--output is required')
    selected=[p for p in candidates if not args.port or p.device.upper()==args.port.upper()]
    if len(selected)!=1: raise SystemExit('Need exactly one expected CM probe CDC port')
    with serial.Serial(selected[0].device,115200,timeout=0.1,write_timeout=1) as port:
        result=run_probe(port,args.start,args.bootsel,args.seconds,args.query)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))
    return 0 if result.get('success',True) else 1


if __name__=='__main__': raise SystemExit(main())
