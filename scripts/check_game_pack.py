"""Compile and run native C decompression/range checks on Windows, offline."""
import argparse
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,default=Path('D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'))
    parser.add_argument('--packed',type=Path,default=ROOT/'artifacts/game/E1M1.t3pg')
    parser.add_argument('--raw',type=Path,default=Path('D:/doom_on_km003c/vendor/dmc100-doom/build/packs/E1M1/DOOM.DAT'))
    parser.add_argument('--breakdown',action='store_true',help='Use the 2 KiB staged benchmark reader and verify path accounting')
    parser.add_argument('--page-totals',action='store_true',help='Check cumulative per-page timing/counts and top-eight reporting')
    parser.add_argument('--lz4',action='store_true',help='Include hybrid codec support (requires --breakdown or --page-totals)')
    parser.add_argument('--prefix-continuation',action='store_true',help='Check the V39/V40 long-code Huffman continuation')
    parser.add_argument('--literal-run',action='store_true',help='Check V44 bounded short-literal batching')
    parser.add_argument('--raw-only',action='store_true',help='Check the cache-free all-raw 2 KiB reader without linking a decoder')
    parser.add_argument('--small-pages',action='store_true',help='Use the 2 KiB staged reader without page-ranking probes')
    parser.add_argument('--reject-packed',type=Path,help='With --raw-only, also reject this valid compressed container')
    args=parser.parse_args()
    if args.breakdown and args.page_totals:parser.error('Choose one profiling mode')
    if args.lz4 and not (args.breakdown or args.page_totals):parser.error('--lz4 requires --breakdown or --page-totals')
    if args.raw_only and (args.breakdown or args.page_totals or args.lz4):parser.error('Raw-only does not support decoder probes')
    if args.reject_packed and not args.raw_only:parser.error('--reject-packed requires --raw-only')
    output=ROOT/'artifacts/host-check'
    output.mkdir(parents=True,exist_ok=True)
    exe=output/('game_pack'+('-raw-only' if args.raw_only else '-small' if args.small_pages else '-breakdown' if args.breakdown else '-pages' if args.page_totals else '')+('-lz4' if args.lz4 else '')+('-literals' if args.literal_run else '')+'.exe')
    extra=[]
    if args.breakdown or args.page_totals:
        extra+=['-DT3P_GAME_PAGE=2048','-DT3P_GAME_SLOTS=35','-DT3P_PROFILE_HOT',
                '-DT3P_PROFILE_STAGES','-DT3P_PROFILE_RAW_RANK',
                '-DT3P_TINF_PREFIX','-DT3P_TINF_LOCAL_BITS']
    if args.breakdown:extra+=['-DT3P_PROFILE_BREAKDOWN']
    if args.page_totals:
        capacity=(args.raw.stat().st_size+2047)//2048
        extra+=['-DT3P_PROFILE_PAGE_TOTALS',f'-DT3P_PROFILE_PAGE_CAPACITY={capacity}']
    if args.prefix_continuation:extra+=['-DT3P_TINF_PREFIX','-DT3P_TINF_LOCAL_BITS','-DT3P_TINF_PREFIX_CONTINUE','-DT3P_TINF_HOT_SECTION']
    if args.literal_run:extra+=['-DT3P_TINF_LITERAL_RUN','-DT3P_TINF_PREFIX','-DT3P_TINF_LOCAL_BITS']
    if args.small_pages or args.raw_only:
        extra+=['-DT3P_GAME_PAGE=2048','-DT3P_GAME_SLOTS=0' if args.raw_only else '-DT3P_GAME_SLOTS=35',
                '-DT3P_PROFILE_HOT','-DT3P_PROFILE_STAGES']
    if args.raw_only:extra+=['-DT3P_GAME_RAW_ONLY']
    if args.lz4:extra+=['-DT3P_GAME_LZ4',str(ROOT/'native/t3p_lz4.c')]
    decoder=[] if args.raw_only else [str(ROOT/'vendor/tinf/tinflate.c')]
    subprocess.run([str(args.compiler),'-std=c11','-Wall','-Wextra','-Werror','-O2',*extra,
                    '-I',str(ROOT/'native'),'-I',str(ROOT/'vendor/tinf'),
                    str(ROOT/'native/t3p_game.c'),*decoder,
                    str(ROOT/'tests/game_pack.c'),'-o',str(exe)],check=True)
    rejected=[str(args.reject_packed)] if args.reject_packed else []
    subprocess.run([str(exe),str(args.packed),str(args.raw),*rejected],check=True)


if __name__=='__main__':
    main()
