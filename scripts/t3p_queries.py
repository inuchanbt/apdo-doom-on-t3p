"""Two fixed stock-application queries; no arbitrary packets or update mode.

Default is offline: print the exact packet without opening USB. --live sends
only the selected query to one identified ATORCH T3P, with bounded sampling.
Measurement polling resets a transient USB-online countdown in the stock app;
neither supported query enters the update, setting-write or motor branches.
"""
import argparse
import datetime
import json
from pathlib import Path
import statistics
import struct
import time


def request(kind):
    prefixes = {
        'settings': bytes.fromhex('55 05 09 03 00 00 00 ee ff'),
        'measurement': bytes.fromhex('55 05 22 05 0b 00 8c ee ff'),
    }
    if kind not in prefixes:
        raise ValueError('Only settings and measurement queries are supported')
    # The stock descriptor has no Report ID item. HIDAPI requires a dummy ID=0
    # for this unnumbered 64-byte report. 0x55 belongs to the payload, not ID.
    return b'\0'+prefixes[kind].ljust(64,b'\0')


def payload(report):
    report=bytes(report)
    if len(report)==65 and report[0]==0:
        return report[1:]
    if len(report)==64:
        return report
    raise ValueError('Expected a 64-byte payload, optionally preceded by ID=0')


def describe(report):
    data=payload(report)
    result=dict(payload_hex=data.hex(),prefix=data[:4].hex(),tail=data[-2:].hex())
    if data[:4]==bytes.fromhex('aa 05 40 03') and data[-2:]==b'\xee\xff':
        result['type']='settings'
        # Offsets are verified; meanings are intentionally left unassigned.
        result['raw_settings']={f'byte_{n}': data[n] for n in [8,9,13]}
    elif data[:4]==bytes.fromhex('aa 40 62 05'):
        # The manufacturer's PC parser at 0x42cc00..0x42ce8d reads little-endian
        # uint32 fields and divides by the float 1,000,000 at 0x478060.
        result['type']='measurement'
        result['scaled_fields']={f'offset_{n:02x}':struct.unpack_from('<I',data,n)[0]/1000000
                                 for n in range(8,52,4)}
        result['byte_60']=data[60]
    else:
        result['type']='unclassified'
    return result


def query(kind, seconds, samples=1, period_ms=200):
    import hid
    devices=[d for d in hid.enumerate(0x0483,0x5750)
             if d.get('usage_page')==0xff02 and d.get('usage')==2
             and d.get('product_string') in ['ATORCH T3P V3.0.2','ATORCH T3P V3.0.3']]
    if len(devices)!=1:
        raise RuntimeError(f'Need exactly one recognized stock T3P; found {len(devices)}')
    info=devices[0]
    device=hid.device()
    reports=[]
    try:
        device.open_path(info['path'])
        # Start from a drained input queue and bound every read.
        for _ in range(16):
            if not device.read(65,1):
                break
        packet=request(kind)
        completed=0
        for sample in range(samples):
            started=time.perf_counter()
            count=device.write(packet)
            if count!=len(packet):
                raise RuntimeError(f'Short HID write: {count}/{len(packet)}')
            matched=False
            for _ in range(128):
                remaining=seconds-(time.perf_counter()-started)
                if remaining<=0:
                    break
                raw=device.read(65,min(200,max(1,int(remaining*1000))))
                if not raw:
                    continue
                try:
                    parsed=describe(raw)
                except ValueError as error:
                    parsed=dict(type='invalid_length',error=str(error))
                reports.append(dict(sample=sample,seconds=round(time.perf_counter()-started,6),
                                    report_hex=bytes(raw).hex(),**parsed))
                if parsed['type']==kind:
                    matched=True
                    completed+=1
                    break
            if not matched:
                break  # No blind retries after a missing or unexpected response.
            if sample+1<samples:
                time.sleep(max(0,period_ms/1000-(time.perf_counter()-started)))
    finally:
        device.close()
    latencies=[r['seconds'] for r in reports if r['type']==kind]
    return dict(captured_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                product=info['product_string'],query=kind,request_hex=packet.hex(),
                samples_requested=samples,samples_completed=completed,
                success=completed==samples,report_count=len(reports),reports=reports,
                median_response_ms=round(statistics.median(latencies)*1000,3) if latencies else None,
                max_response_ms=round(max(latencies)*1000,3) if latencies else None)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--query',choices=['settings','measurement'],default='settings')
    parser.add_argument('--seconds',type=float,default=2)
    parser.add_argument('--samples',type=int,default=1)
    parser.add_argument('--period-ms',type=int,default=200)
    parser.add_argument('--live',action='store_true')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if not 0.1<=args.seconds<=10:
        parser.error('--seconds must be between 0.1 and 10')
    if not 1<=args.samples<=50 or not 50<=args.period_ms<=1000:
        parser.error('Use 1..50 samples and a period of 50..1000 ms')
    if not args.live:
        print(json.dumps(dict(mode='OFFLINE; USB not opened',query=args.query,
                              request_hex=request(args.query).hex()),indent=2))
        return
    result=query(args.query,args.seconds,args.samples,args.period_ms)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    summary={k:v for k,v in result.items() if k!='reports'}
    if len(result['reports'])==1:
        summary['reports']=result['reports']
    print(json.dumps(summary,indent=2))
    return 0 if result['success'] else 1


if __name__=='__main__':
    raise SystemExit(main())
