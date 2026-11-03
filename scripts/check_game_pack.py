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
    args=parser.parse_args()
    output=ROOT/'artifacts/host-check'
    output.mkdir(parents=True,exist_ok=True)
    exe=output/'game_pack.exe'
    subprocess.run([str(args.compiler),'-std=c11','-Wall','-Wextra','-Werror','-O2',
                    '-I',str(ROOT/'native'),'-I',str(ROOT/'vendor/tinf'),
                    str(ROOT/'native/t3p_game.c'),str(ROOT/'vendor/tinf/tinflate.c'),
                    str(ROOT/'tests/game_pack.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(args.packed),str(args.raw)],check=True)


if __name__=='__main__':
    main()
