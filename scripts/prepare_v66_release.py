"""Assemble the V66 app/packer kit and matching source without stock firmware."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def tracked(folder):
    return subprocess.check_output(
        ['git', '-C', str(folder), 'ls-files', '-z']).decode().rstrip('\0').split('\0')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'artifacts/native-doom-v66-prebuilt-public')
    parser.add_argument('--converter-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/release-v66')
    args = parser.parse_args()
    manifest = json.loads((args.build / 'manifest.json').read_bytes())
    payload = (args.build / 'DO-NOT-FLASH-app-payload.bin').read_bytes()
    identity = manifest['usb_identity']
    if (manifest['revision'] != 66 or manifest['jieli_runtime_archives_linked'] or
            identity['vid_pid'] != '16c0:05df' or not identity['shared'] or
            identity['private_test'] or not identity['manufacturer']):
        raise ValueError('Expected V66 with the documented shared HID identity and no JieLi runtime archives')
    if digest(payload) != manifest['payload_sha256']:
        raise ValueError('DOOM app digest differs from its build manifest')
    for name, expected in manifest['source_sha256'].items():
        if digest((ROOT / 'native' / name).read_bytes()) != expected:
            raise ValueError(f'Source has changed since the build: {name}')
    files = tracked(ROOT)
    upstream = args.converter_root / 'oracle/src/doomgeneric-repo'
    release = {
        'version': '66',
        'payload_sha256': manifest['payload_sha256'],
        'image_sha256': manifest['image_sha256'],
        'usb': {**identity, 'product': 'T3P DOOM V66'},
        'hardware_status': 'V66 gameplay and playable live preview confirmed with the private test ID; shared-ID enumeration pending',
    }
    release_info = json.dumps(release, indent=2) + '\n'
    args.output.mkdir(parents=True, exist_ok=True)
    kit = args.output / 't3p-doom-v66-kit.zip'
    source = args.output / 't3p-doom-v66-source.zip'
    kit_files = {
        'COPYING', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
        'vendor/dmc100-doom/COPYING', 'vendor/dmc100-doom/CREDITS.md',
        'docs/PREBUILT.md', 'docs/PREBUILT.ja.md', 'docs/USB_ID.md',
        'docs/RECOVERY_DECISION.ja.md', 'analysis/native-doom-v66-physical-result.json',
        'scripts/package_prebuilt.py', 'scripts/check_stock_app_container.py',
        'scripts/audit_stock_recovery.py', 'scripts/find_stock_calls.py',
        'scripts/t3p_pc_keyboard.py', 'scripts/t3p_pc_controller.py',
    }
    prefixes = ('vendor/jl-misctools/', 'vendor/dmc100-doom/LICENSES/',
                'notices/freedoom-0.13.0/', 'web/t3p/')
    with ZipFile(kit, 'w', ZIP_DEFLATED) as archive:
        for name in files:
            if name in kit_files or name.startswith(prefixes):
                archive.write(ROOT / name, name)
        archive.writestr('DO-NOT-FLASH-t3p-doom-app.bin', payload)
        archive.writestr('release.json', release_info)
        archive.writestr('START-HERE.txt', 'English: docs/PREBUILT.md\nJapanese: docs/PREBUILT.ja.md\n')
    with ZipFile(source, 'w', ZIP_DEFLATED) as archive:
        for name in files:
            archive.write(ROOT / name, name)
        for name in tracked(args.converter_root):
            archive.write(args.converter_root / name, 'converter/' + name)
        for name in tracked(upstream):
            if name.startswith('doomgeneric/') or name == 'LICENSE':
                archive.write(upstream / name, 'converter/oracle/src/doomgeneric-repo/' + name)
        for name in ('dl/freedoom-0.13.0.zip', 'build/conv/upstream.json'):
            archive.write(args.converter_root / name, 'converter/' + name)
        archive.writestr('release.json', release_info)
    for path in (kit, source):
        with ZipFile(path) as archive:
            if archive.testzip() is not None or any(
                    name.startswith(('Atorch/', 'artifacts/', 'local/')) for name in archive.namelist()):
                raise ValueError('Unexpected or corrupt archive content')
    sums = ''.join(f'{digest(path.read_bytes())}  {path.name}\n' for path in (kit, source))
    (args.output / 'SHA256SUMS.txt').write_text(sums, encoding='ascii')
    print(sums, end='')


if __name__ == '__main__':
    main()
