"""Create a DcTester image offline using your own exact ATORCH T3P 3.0.3."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from check_stock_app_container import rebuild_in_memory

ROOT = Path(__file__).resolve().parents[1]
STOCK_SHA256 = '72571d747e9da86194deddae3802992f846415c45b82d7b28f8aa567af30acb2'
PAYLOAD_NAME = 'DO-NOT-FLASH-t3p-doom-app.bin'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def package(stock, payload, manifest):
    if len(stock) != 1540096 or digest(stock) != STOCK_SHA256:
        raise ValueError(f'Stock file must be the exact ATORCH T3P 3.0.3 (SHA-256 {STOCK_SHA256})')
    if len(payload) != 1465664 or digest(payload) != manifest['payload_sha256']:
        raise ValueError('DOOM app does not match this kit')

    with tempfile.TemporaryDirectory(prefix='t3p-doom-') as directory:
        source = Path(directory) / 'stock.bin'
        source.write_bytes(stock)
        unpacker = ROOT / 'vendor/jl-misctools/firmware/fwunpack_newfw.py'
        result = subprocess.run([sys.executable, str(unpacker), '--dirname', '{fdir}/unpacked', str(source)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        extracted = source.parent / 'unpacked'
        try:
            info = json.loads((extracted / 'jlfw.yaml').read_text(encoding='utf-8'))
            plain = (extracted / 'decrypted.bin').read_bytes()
            key = info['chip-key']
            if result.returncode or info['base-offset'] != 0 or info['entry-point'] != 0x0c000100:
                raise ValueError('Unexpected stock firmware layout')
        except (OSError, ValueError, KeyError) as error:
            raise ValueError('Could not unpack the stock firmware') from error

    image, _, _ = rebuild_in_memory(stock, plain, key, payload)
    if digest(image) != manifest['image_sha256']:
        raise ValueError('Generated image differs from the reference build')
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stock', type=Path, help='path to your original T3P 3.0.3 .bin')
    parser.add_argument('--kit', type=Path, default=ROOT, help='directory containing the app and release.json')
    parser.add_argument('--output', type=Path, help='new output file (defaults to the versioned DcTester filename)')
    args = parser.parse_args()
    try:
        manifest = json.loads((args.kit / 'release.json').read_text(encoding='utf-8'))
        version = manifest['version']
        if not isinstance(version, str) or not re.fullmatch(r'[0-9]+(?:\.[0-9]+)?', version):
            raise ValueError('Invalid kit version')
        destination = args.output or Path(f'T3P_NATIVE_DOOM_E1M1_V{version}_3.0.3.bin')
        image = package(args.stock.read_bytes(), (args.kit / PAYLOAD_NAME).read_bytes(), manifest)
        with destination.open('xb') as output:
            output.write(image)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'Error: {error}\n')
    print(f'{destination}\nSHA-256 {digest(image)}')


if __name__ == '__main__':
    main()
