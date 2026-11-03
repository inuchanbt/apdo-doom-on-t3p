"""Link the pi32v2 DOOM archive against placeholder callbacks, offline only.

The resulting ELF proves symbol resolution and reports static sizes. It is
not a T3P firmware image and must never be packaged or flashed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from build_engine import section_sizes

ROOT = Path(__file__).resolve().parents[1]


def region_spans(elf):
    data=elf.read_bytes()
    phoff,shoff=struct.unpack_from('<II',data,28)
    phsize,phcount,shsize,shcount=struct.unpack_from('<4H',data,42)
    flash_end=0x0c000100
    ram_end=0x101a80
    for pos in range(phoff,phoff+phsize*phcount,phsize):
        kind,_,_,physical,filesize,_,_,_=struct.unpack_from('<8I',data,pos)
        if kind==1 and filesize and 0x0c000100<physical+filesize<=0x0c000100+0x1f0000:
            flash_end=max(flash_end,physical+filesize)
    for pos in range(shoff,shoff+shsize*shcount,shsize):
        _,_,flags,address,_,size=struct.unpack_from('<6I',data,pos)
        if flags&3==3 and 0x101a80<=address<0x136e00:
            ram_end=max(ram_end,address+size)
    return flash_end-0x0c000100,ram_end-0x101a80


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, default=ROOT/'local/tools/jieli/C$/JL/pi32')
    parser.add_argument('--engine-dir', type=Path, default=ROOT/'artifacts/pi32v2-engine')
    parser.add_argument('--game-pack', type=Path, default=ROOT/'artifacts/game/E1M1.t3pg')
    parser.add_argument('--runtime-layout',action='store_true',
                        help='Offline CPU/memory entry fixture with explicit driver/stack budgets; still NOT flashable')
    args = parser.parse_args()
    toolchain, output = args.toolchain.resolve(), args.engine_dir.resolve()
    archive = output/'libt3pdoom.a'
    if not archive.is_file():
        parser.error('Run build_engine.py first')
    if args.runtime_layout:
        output=output/'runtime-layout'
        output.mkdir(exist_ok=True)
    pack=args.game_pack.resolve()
    if not pack.is_file():
        parser.error('Run pack_game.py first')
    game_asm,game_obj=output/'game_pack.s',output/'game_pack.o'
    game_asm.write_text('.section .game,"a",@progbits\n.balign 4\n.global t3p_game_image\n'
                        't3p_game_image:\n.incbin '+json.dumps(pack.as_posix())+'\n'
                        '.global t3p_game_image_end\nt3p_game_image_end:\n',encoding='utf-8')
    subprocess.run([str(toolchain/'bin/clang.exe'),'-target','pi32v2','-mcpu=r3',
                    '-integrated-as','-c',str(game_asm),'-o',str(game_obj)],check=True)
    obj, elf, mapfile = output/'linkcheck_platform.o', output/('DO-NOT-FLASH-runtime-layout.elf' if args.runtime_layout else 'DO-NOT-FLASH-linkcheck.elf'), output/'linkcheck.map'
    subprocess.run([str(toolchain/'bin/clang.exe'), '-target', 'pi32v2', '-mcpu=r3',
                    '-integrated-as', '-O2', '-ffreestanding', '-fno-builtin', '-Wno-macro-redefined',
                    '-ffunction-sections', '-fdata-sections', '-mllvm', '-pi32v2-large-program=true',
                    '-I', str(ROOT/'vendor/dmc100-doom/engine/stub/include'),
                    *(['-DT3P_RUNTIME_LAYOUT'] if args.runtime_layout else []),
                    '-I', str(toolchain/'include/compiler'), '-c', str(ROOT/'native/linkcheck_platform.c'),
                    '-o', str(obj)], check=True)
    lib = toolchain/'pi32v2-lib/r3-large'
    startup=[]
    if args.runtime_layout:
        for name in ['runtime_start.S','runtime_memory.c','t3p_signal.c','t3p_boot.c']:
            target=output/(name+'.o')
            subprocess.run([str(toolchain/'bin/clang.exe'),'-target','pi32v2','-mcpu=r3',
                            '-integrated-as','-O2','-ffreestanding','-fno-builtin',
                            '-Wno-macro-redefined','-Wno-unused-command-line-argument',
                            '-ffunction-sections','-fdata-sections','-mllvm',
                            '-pi32v2-large-program=true','-I',str(toolchain/'include/compiler'),
                            *(['-x','assembler'] if name.endswith('.S') else []),
                            '-c',str(ROOT/'native'/name),'-o',str(target)],check=True)
            startup.append(str(target))
    command = [str(toolchain/'bin/pi32v2-ld.exe'), '-T', str(ROOT/'native'/('runtime_layout.ld' if args.runtime_layout else 'linkcheck.ld')),
               '--gc-sections', '-Map='+str(mapfile), '-o', str(elf), str(obj), str(game_obj), str(archive),
               *startup,'--start-group', str(lib/'libcompiler-rt.a'), str(lib/'libm.a'), str(lib/'libc.a'), '--end-group']
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace')
    (output/'linkcheck.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    if result.returncode:
        raise SystemExit(result.stdout+result.stderr)
    sizes = section_sizes(elf)
    flash_span,ram_span=region_spans(elf)
    manifest = dict(status='OFFLINE LINK PROBE ONLY; no device execution',
                    target='pi32v2', cpu='r3', sizes=sizes,
                    embedded_game_bytes=pack.stat().st_size,
                    flash_total_bytes=flash_span,
                    flash_region_bytes=0x1f0000,
                    flash_remaining_before_startup_and_drivers=0x1f0000-flash_span,
                    static_ram_bytes=sizes['data']+sizes['bss'],
                    static_ram_span_with_alignment=ram_span,
                    ram_region_bytes=0x35380,
                    ram_remaining_before_stack_and_drivers=0x35380-ram_span,
                    elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest())
    if args.runtime_layout:
        from runtime_layout_evidence import inspect_layout
        layout=inspect_layout(elf)
        (output/'layout-audit.json').write_text(json.dumps(layout,indent=2)+'\n',encoding='utf-8')
        with (output/'runtime-layout.disasm.txt').open('w',encoding='utf-8') as stream:
            subprocess.run([str(toolchain/'bin/llvm-objdump.exe'),'-d','-print-imm-hex',str(elf)],
                           stdout=stream,check=True)
        manifest.update(status='OFFLINE STARTUP/MEMORY LAYOUT FIXTURE; hardware callbacks are placeholders; DO NOT FLASH',
                        startup_cpu_memory_entry=True,hardware_initialization=False,
                        driver_flash_budget_bytes=0x18000,driver_ram_budget_bytes=0x8000,
                        stack_budget_bytes=0x4000,flash_region_bytes=0x165d40,
                        flash_remaining_before_startup_and_drivers=None,
                        ram_remaining_before_stack_and_drivers=None,layout=layout,
                        static_ram_bytes=layout['actual_static_ram_bytes'],
                        static_ram_span_with_alignment=None,
                        allocated_section_totals_including_reservations=sizes,
                        sizes=dict(code=sizes['code'],rodata=sizes['rodata'],data=sizes['data'],
                                   bss=layout['actual_static_ram_bytes']-sizes['data']),
                        flash_total_bytes=layout['flash_used_including_driver_budget'])
    (output/'linkcheck.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
