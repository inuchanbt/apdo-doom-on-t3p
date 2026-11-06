"""Compile the existing low-memory DOOM port for AC707N/pi32v2, offline.

No SDK post-build script, USB access, packaging, or firmware flashing is run.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'vendor/dmc100-doom'


def section_sizes(path):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 0xf1:
        raise ValueError(f'Not a little-endian pi32v2 ELF object: {path}')
    offset = struct.unpack_from('<I', data, 32)[0]
    size, count = struct.unpack_from('<HH', data, 46)
    totals = dict(code=0, rodata=0, data=0, bss=0)
    for pos in range(offset, offset+size*count, size):
        _, kind, flags, _, _, length = struct.unpack_from('<6I', data, pos)
        if not flags & 2:
            continue
        category = 'bss' if kind == 8 else 'code' if flags & 4 else 'data' if flags & 1 else 'rodata'
        totals[category] += length
    return totals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, default=ROOT / 'local/tools/jieli/C$/JL/pi32')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--host-test', action='store_true')
    parser.add_argument('--host-compiler', type=Path, default=Path('D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'))
    parser.add_argument('--opt', choices=['O2', 'Oz'], default='O2')
    parser.add_argument('--game-page-bytes', type=int, choices=[2048,4096], default=4096)
    parser.add_argument('--raw-game-only', action='store_true', help='Cache-free reader; accepts only all-raw game pages')
    parser.add_argument('--weapon-view-y', action='store_true', help='Keep original DOOM weapon Y scale in the square viewport')
    parser.add_argument('--fixed-tic-benchmark', action='store_true', help='Host: native-style indexed surface and exactly one game tic per frame')
    parser.add_argument('--present-mode', choices=['serial','overlap'], help='Host: shared double-buffer presenter with deferred DMA reads')
    parser.add_argument('--seamless-view', action='store_true', help='Host: fill the message band with the 3D view')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 4, 8))
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error('--jobs must be between 1 and 16')
    if args.fixed_tic_benchmark and not args.host_test:
        parser.error('--fixed-tic-benchmark is for the host harness only')
    if args.present_mode and not (args.host_test and args.raw_game_only):
        parser.error('--present-mode requires --host-test --raw-game-only')
    if args.seamless_view and not args.present_mode:
        parser.error('--seamless-view requires --present-mode')
    toolchain = args.toolchain.resolve()
    output = (args.output or ROOT/('artifacts/host-engine' if args.host_test else 'artifacts/pi32v2-engine')).resolve()
    compiler = args.host_compiler.resolve() if args.host_test else toolchain / 'bin/clang.exe'
    archiver = toolchain / 'bin/llvm-ar.exe'
    if not compiler.is_file() or (not args.host_test and not archiver.is_file()):
        parser.error('Extract the official Windows toolchain first')
    output.mkdir(parents=True, exist_ok=True)
    excluded = {'am_map.c', 'w_wad.c', 'w_file.c', 'z_zone.c'}
    sources = [p for p in (SRC/'engine').glob('*.c') if p.name not in excluded]
    for folder in ['port/data', 'port/render', 'port/obj', 'port/math']:
        sources += [p for p in (SRC/folder).glob('*.c')
                    if p.name not in {'img.c', 'img_check.c', 'fat_write.c', 'fat12.c', 'extents.c'}]
    sources += [SRC/p for p in ['port/game/fe_glue.c', 'port/game/l_menu.c',
        'port/game/l_wi.c', 'port/game/l_finale.c', 'port/game/i_input.c',
        'port/game/libc_dmc.c', 'platform/rt.c', 'platform/text.c', 'platform/font8x8.c']]
    sources += sorted((SRC/'gen').glob('*.c'))
    sources += [ROOT/'native/doom_bridge.c', ROOT/'native/square_view.c']
    sources += [ROOT/'native/t3p_input.c', ROOT/'native/t3p_sensors.c', ROOT/'native/t3p_game.c', ROOT/'native/t3p_display.c',
                ROOT/'vendor/tinf/tinflate.c']
    if args.raw_game_only:
        sources.remove(ROOT/'vendor/tinf/tinflate.c')
    if args.host_test:
        sources += [ROOT/'native/host_harness.c', ROOT/'tests/host_play_probe.c']
    if args.present_mode:
        sources += [ROOT/'native/t3p_present.c']
    includes = ['engine/stub/include', 'engine', 'port/data', 'port/render', 'port/obj',
                'port/math', 'platform', 'port/game', 'port/ovl', 'gen']
    flags = ['-'+args.opt, '-std=gnu11', '-ffreestanding', '-fno-common',
             '-fno-builtin', '-fwrapv', '-fsigned-char', '-ffunction-sections', '-fdata-sections',
             '-DDMC_FW', '-DPLAT_HOST', '-DDMC_SINE_L1', '-DKM_CACHE_KIB=4',
             '-DKM_INPUT_AT_FRAME', '-DKM_SQUARE_VIEW', '-w']
    for inc in includes:
        flags += ['-I', str(SRC/inc)]
    flags += ['-I', str(ROOT/'vendor/tinf')]
    flags += [f'-DT3P_GAME_PAGE={args.game_page_bytes}']
    if args.game_page_bytes==2048 or args.raw_game_only:
        flags += ['-DT3P_PROFILE_HOT']
    if args.raw_game_only:
        flags += ['-DT3P_GAME_RAW_ONLY','-DT3P_GAME_SLOTS=0']
    if args.fixed_tic_benchmark:
        flags += ['-DT3P_BENCH_FIXED_TICS']
    if args.fixed_tic_benchmark or args.present_mode:
        flags += ['-DT3P_INDEXED_FRAME']
    if args.present_mode:
        flags += ['-DT3P_FRAME_PRESENTER']
        if args.present_mode=='overlap':flags += ['-DT3P_PRESENT_OVERLAP']
    if args.seamless_view:
        flags += ['-DT3P_SEAMLESS_VIEW']
    if not args.host_test:
        flags = ['-target','pi32v2','-mcpu=r3','-integrated-as','-mllvm','-pi32v2-large-program=true']+flags
        flags += ['-I',str(toolchain/'include/compiler'),'-I',str(toolchain/'pi32v2-include')]

    def compile_one(source):
        obj = output / (str(source.relative_to(ROOT)).replace('\\', '_').replace('/', '_')+'.o')
        # tinf's input/buffer error checks remain enabled; its internal invariant
        # asserts need no target libc abort handler in this freestanding build.
        extra = ['-DNDEBUG'] if source.name == 'tinflate.c' else []
        if args.weapon_view_y and source == SRC/'engine/r_things.c':
            extra += ['-DT3P_WEAPON_VIEW_Y']
        if args.host_test and source.name not in {'host_harness.c','tinflate.c'}:
            extra += ['-U_WIN32']
        result = subprocess.run([str(compiler), *flags, *extra, '-c', str(source), '-o', str(obj)],
                                capture_output=True, text=True, encoding='utf-8', errors='replace')
        if result.returncode:
            raise RuntimeError(f'{source}\n{result.stdout}{result.stderr}')
        return obj

    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        objects = list(workers.map(compile_one, sources))
    if args.host_test:
        library=output/'t3pdoom-test.dll'
        subprocess.run([str(compiler),'-shared','-Wl,--gc-sections',*map(str,objects),'-o',str(library)],check=True)
        print(f'Built Windows simulator {library} ({len(objects)} objects); not device execution')
        return
    archive = output/'libt3pdoom.a'
    if archive.exists():
        archive.unlink()
    subprocess.run([str(archiver), 'rcs', str(archive), *map(str, objects)], check=True)
    sizes = {str(p.relative_to(output)): section_sizes(p) for p in objects}
    totals = {k: sum(s[k] for s in sizes.values()) for k in ['code', 'rodata', 'data', 'bss']}
    manifest = dict(target='pi32v2', cpu='r3', optimization=args.opt, objects=len(objects),
                    compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(),
                    archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                    unlinked_section_totals=totals, sections=sizes,
                    status='Compiled archive only; not a bootable device firmware')
    (output/'build.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print('Built', archive, 'objects:', len(objects))
    print('Section totals before linking:', json.dumps(totals))


if __name__ == '__main__':
    main()
