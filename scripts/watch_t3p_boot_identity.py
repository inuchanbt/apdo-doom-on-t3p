"""Observe Windows HID identity only; no HID handles or output reports.

Default is OFFLINE. --live enumerates the user-connected device and saves
public identity fields, excluding serial numbers and Windows device paths.
Mode labels match supplied descriptors; they are not flash/ROM readbacks.
"""
import argparse
import datetime
import json
from pathlib import Path
import time

def classify(info):
    if (info.get('vendor_id'),info.get('product_id'))!=(0x0483,0x5750):return None
    usage=(info.get('usage_page'),info.get('usage'))
    if usage==(0xff02,2):return 'stock_application_profile'
    if usage==(0xff00,1):return 'stock_HID_bootloader_profile'
    return 'unclassified_same_vid_pid'

def public_identity(info):
    mode=classify(info)
    if mode is None:return None
    return dict(vid='0483',pid='5750',usage_page=info.get('usage_page'),usage=info.get('usage'),
        manufacturer=info.get('manufacturer_string'),product=info.get('product_string'),
        interface=info.get('interface_number'),mode=mode)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--live',action='store_true')
    p.add_argument('--seconds',type=int,default=600)
    p.add_argument('--output',type=Path)
    a=p.parse_args()
    if not 2<=a.seconds<=3600:p.error('Use2..3600seconds')
    if not a.live:
        print(json.dumps(dict(mode='OFFLINE',application_usage='ff02:0002',
            bootloader_usage='ff00:0001',device_handles_opened=0,output_reports_sent=0)))
        return 0
    if a.output is None:p.error('--output required with --live')
    import hid
    result=dict(scope='Windows HID identity enumeration only; device boot may itself run stock startup code',
        events=[],device_handles_opened=0,output_reports_sent=0,boot_profile_seen=False,
        source_fingerprint='analysis/stock-recovery-audit.json',success=False)
    deadline=time.monotonic()+a.seconds;previous=None;stable=0
    while time.monotonic()<deadline:
        identities=[public_identity(x) for x in hid.enumerate(0x0483,0x5750)]
        identities=[x for x in identities if x is not None]
        identities.sort(key=lambda x:json.dumps(x,sort_keys=True))
        signature=json.dumps(identities,sort_keys=True)
        if signature!=previous:
            result['events'].append(dict(at=datetime.datetime.now(datetime.timezone.utc).isoformat(),devices=identities))
            a.output.parent.mkdir(parents=True,exist_ok=True)
            a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            print(json.dumps(dict(event='identity_change',devices=identities)),flush=True)
            previous=signature;stable=0
        stable+=1
        if len(identities)==1 and identities[0]['mode']=='stock_HID_bootloader_profile' and stable>=2:
            result.update(success=True,boot_profile_seen=True);break
        time.sleep(1)
    result['finished']=True
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(success=result['success'],boot_profile_seen=result['boot_profile_seen'],
                         device_handles_opened=0,output_reports_sent=0)),flush=True)
    return 0 if result['success'] else 1

if __name__=='__main__':raise SystemExit(main())
