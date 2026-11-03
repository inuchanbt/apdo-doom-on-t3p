"""Passively read the auxiliary RP2350 CM diagnostic's serial JSON output.

No serial bytes are transmitted. The diagnostic's 1200-baud auto-reset is
disabled. Use only the port of a board running cm_input_check.
"""
import argparse
import json
from pathlib import Path
import time


def main():
    import serial
    from serial.tools import list_ports
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--seconds',type=float,default=5)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if not 1<=args.seconds<=30:
        parser.error('Use 1..30 seconds')
    if not args.port:
        print(json.dumps([dict(port=p.device,description=p.description)
                          for p in list_ports.comports() if p.vid==0x2e8a],indent=2))
        return 0
    samples=[]
    with serial.Serial(args.port,115200,timeout=0.25) as port:
        started=time.monotonic()
        line=bytearray()
        while time.monotonic()-started<args.seconds:
            part=port.read(256)
            for byte in part:
                if byte==10:
                    try:
                        item=json.loads(line.decode('ascii').strip())
                    except (ValueError,UnicodeDecodeError):
                        item=None
                    line.clear()
                    if isinstance(item,dict) and item.get('program')=='t3p_cm_input_check':
                        if (item.get('mode'),item.get('board'),item.get('dp_gpio'),item.get('dm_gpio'))!=('inputs_only','RP2350-USB-CM',13,12):
                            raise ValueError('Unexpected board diagnostic profile')
                        samples.append(item)
                elif len(line)<1024:
                    line.append(byte)
                else:
                    raise ValueError('Serial line too long')
    result=dict(status='Auxiliary CM input capture; no target data transmitted',
                samples=samples,valid_samples=len(samples),success=len(samples)>=2)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))
    return 0 if result['success'] else 1


if __name__=='__main__':
    raise SystemExit(main())
