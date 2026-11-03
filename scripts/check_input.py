"""Build and run Windows host checks with synthetic sensor data, offline."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,default=Path('D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'))
    args=parser.parse_args()
    output=ROOT/'artifacts/host-check'
    output.mkdir(parents=True,exist_ok=True)
    exe=output/'input_sensors.exe'
    subprocess.run([str(args.compiler),'-std=c11','-Wall','-Wextra','-Werror','-O2',
                    '-I',str(ROOT/'native'),str(ROOT/'native/t3p_input.c'),
                    str(ROOT/'native/t3p_sensors.c'),str(ROOT/'tests/input_sensors.c'),
                    '-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)


if __name__=='__main__':
    main()
