"""Offline inventory and string extraction; never accesses a USB device."""
import argparse
import binascii
import collections
import hashlib
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def fingerprint(path):
    data = path.read_bytes()
    counts = collections.Counter(data)
    return dict(file=str(path.relative_to(ROOT)), size=len(data),
                sha256=hashlib.sha256(data).hexdigest(),
                entropy=round(-sum(n / len(data) * math.log2(n / len(data)) for n in counts.values()), 6),
                header_hex=data[:32].hex())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=ROOT / 'local/unpacked/T3P Firmware Version 3.0.3.bin/files/app.bin')
    args = parser.parse_args()
    out = ROOT / 'analysis'
    out.mkdir(exist_ok=True)
    files = sorted((ROOT / 'Atorch').glob('*.bin'))
    files.append(ROOT / 'Atorch/01-T3P_PC_Application_V1.0.9/DcTester.exe')
    inventory = [fingerprint(p) for p in files]
    (out / 'stock-inventory.json').write_text(json.dumps(inventory, ensure_ascii=False, indent=2), encoding='utf-8')
    data = args.app.read_bytes()
    strings = [dict(offset=m.start(), address=0x0c000100+m.start(), text=m[0].decode('ascii'))
               for m in re.finditer(rb'[ -~]{5,}', data)]
    (out / 'app-3.0.3-strings.json').write_text(json.dumps(strings, indent=2), encoding='utf-8')
    interesting = re.compile(r'lcd|st77|gc9|touch|cst|axs|ft6|hid|usb|iic|i2c|bmp|spl06|qmi|sc7|mpu|lis|vibr|motor|br35|version|T3P|ui_', re.I)
    selected = [s for s in strings if interesting.search(s['text'])]
    lines = ['0x%08x  %s' % (s['address'], s['text']) for s in selected]
    (out / 'app-3.0.3-interesting.txt').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print('\n'.join(lines))
    print('app size:', len(data), 'CRC16: 0x%04x' % binascii.crc_hqx(data, 0))
    print('app first 128 bytes:', data[:128].hex(' '))


if __name__ == '__main__':
    main()
