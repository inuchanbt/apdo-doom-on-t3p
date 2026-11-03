"""Verify all four local stock extractions and their cipher round trip offline."""
import binascii
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'vendor/jl-misctools/firmware'))
from jltech.cipher import jl_enc_cipher


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    inventory = json.loads((ROOT / 'analysis/stock-inventory.json').read_text(encoding='utf-8'))
    results = []
    for item in inventory:
        source = ROOT / item['file']
        original = source.read_bytes()
        require(hashlib.sha256(original).hexdigest() == item['sha256'], f'Source changed: {source.name}')
        if source.suffix != '.bin':
            continue
        directory = ROOT / 'local/unpacked' / source.name
        info = json.loads((directory / 'jlfw.yaml').read_text(encoding='utf-8'))
        require(info['base-offset'] == 0 and info['entry-point'] == 0x0c000100,
                f'Unexpected T3P layout: {source.name}')
        decoded = (directory / 'decrypted.bin').read_bytes()
        hcrc, dcrc, app_offset, app_size, flags, reserved, index, name = struct.unpack_from('<HHIIBBH16s', decoded, 0x5020)
        require(binascii.crc_hqx(decoded[0x5022:0x5040], 0) == hcrc, 'App entry CRC mismatch')
        require(name.split(b'\0', 1)[0] == b'app.bin' and flags == 0x82 and app_offset == 0x100,
                'Unexpected app entry')
        start = 0x5000 + app_offset
        require(start+app_size <= len(original), 'App exceeds source file')
        app = (directory / 'files/app.bin').read_bytes()
        require(len(app) == app_size and app == decoded[start:start+app_size], 'Extracted app differs')
        require(binascii.crc_hqx(app, 0) == dcrc, 'App payload CRC mismatch')
        encrypted = bytearray(app)
        for off in range(0, len(encrypted), 32):
            jl_enc_cipher(encrypted, off, min(32, len(encrypted)-off), info['chip-key'] ^ ((app_offset+off) >> 2))
        require(encrypted == original[start:start+app_size], 'Cipher round trip differs from stock')
        result = dict(file=source.name, source_sha256=item['sha256'], app_size=app_size,
                      app_crc16=f'{dcrc:04x}', app_sha256=hashlib.sha256(app).hexdigest(),
                      uboot_sha256=hashlib.sha256((directory / 'top/uboot.boot').read_bytes()).hexdigest(),
                      entry_crc_valid=True, payload_crc_valid=True, cipher_roundtrip_identical=True)
        results.append(result)
        print(f'{source.name}: app {app_size} bytes, CRC16 {dcrc:04x}, cipher round trip OK')
    require(len(results) == 4, 'Expected the four supplied T3P stock firmware files')
    (ROOT / 'analysis/stock-verification.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
