"""Build the E1M1 low-resolution asset pair offline, without accessing a device.

Requires the full pinned DMC100 converter checkout and its cached Freedoom ZIP;
the publication checkout contains the engine subset only. No downloads occur.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

from pack_game import pack as page_pack, verify

ROOT = Path(__file__).resolve().parents[1]
BASELINE_SHA = '6b25a4f1adb6f1f1f5f42226c272b93ccff4f356ac8d7034cf83712b2da70c08'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--converter-root', type=Path,
                        default=Path('D:/doom_on_km003c/vendor/dmc100-doom'))
    parser.add_argument('--output', type=Path, default=ROOT/'artifacts/game-lowres')
    args = parser.parse_args()
    upstream = args.converter_root.resolve()
    archive = upstream/'dl/freedoom-0.13.0.zip'
    if not archive.is_file():
        parser.error('The cached Freedoom 0.13.0 ZIP is required; this script does not download it')
    sys.path.insert(0, str(upstream))
    from tools.conv import fetch, info, pack, plan, wadread
    raw_wad, _ = fetch.extract(str(archive))
    wad = wadread.Wad(raw_wad)
    tables = json.loads((upstream/'build/conv/upstream.json').read_text(encoding='utf-8'))
    deh = wadread.dehacked(wad)
    inf, dense = info.build(wad, tables, deh)
    ctx = pack.Ctx(wad, raw_wad, [], tables, deh, inf, dense, plan.load_layout(None))
    baseline = dict(pack.PACKS['E1M1'])
    built = pack.build_image(ctx, 'E1M1', baseline, {})
    reference, _, _ = pack.finish(ctx, 'E1M1', built)
    if sha(reference) != BASELINE_SHA:
        raise ValueError('Converter baseline differs from the existing T3P E1M1 data')
    cfg = {**baseline, 'base': 1, 'mips': 1, 'xshift': 1, 'ui_exclude': ('HELP1',)}
    built = pack.build_image(ctx, 'E1M1', cfg, {})
    raw, _, _ = pack.finish(ctx, 'E1M1', built)
    count = (len(raw)+2047)//2048
    images = {
        'E1M1-deflate.t3pg': page_pack(raw, 2048, try_strategies=True),
        'E1M1-all-raw.t3pg': page_pack(raw, 2048, raw_pages=set(range(count))),
    }
    result = dict(
        status='OFFLINE ASSET PAIR ONLY; image quality and device performance untested',
        source_wad_sha256=sha(raw_wad), baseline_raw_sha256=sha(reference),
        raw_bytes=len(raw), raw_sha256=sha(raw), page_bytes=2048, pages=count,
        config_changes=dict(base=1, mips=1, xshift=1, ui_exclude=['HELP1']),
        sections=built.report['sections'], maps=built.report['maps'],
        converter_sources_sha256={p.relative_to(upstream).as_posix():sha(p.read_bytes())
                                  for p in sorted((upstream/'tools/conv').glob('*.py'))},
        game_tables_sha256=sha((upstream/'build/conv/upstream.json').read_bytes()),
        images={}, notes=[
            'Both containers restore the exact same low-resolution raw image.',
            'Palette and E1M1 are retained. Wall textures lose fine detail and sprite columns are halved; monster row detail and HUD assets are retained.',
            'HELP1 is omitted using the upstream converter exclusion mechanism.',
            'All-raw pages retain first-use CRC checks in the target reader.',
        ])
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'DOOM.DAT').write_bytes(raw)
    for name, image in images.items():
        verify(image, raw, 2048)
        flags = [struct.unpack_from('<I', image, 64+16*p+4)[0] for p in range(count)]
        raw_pages = sum(bool(flag&0x80000000) for flag in flags)
        if name == 'E1M1-all-raw.t3pg':
            assert raw_pages == count
        else:
            assert raw_pages < count
        (args.output/name).write_bytes(image)
        result['images'][name] = dict(bytes=len(image), sha256=sha(image),
                                     raw_pages=raw_pages, deflate_pages=count-raw_pages,
                                     full_python_roundtrip=True)
    (args.output/'manifest.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({key:result[key] for key in ('raw_bytes','raw_sha256','pages','images')}, indent=2))


if __name__ == '__main__':
    main()
