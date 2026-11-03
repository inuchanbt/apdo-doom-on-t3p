"""Exercise the native probe's buffer ownership and SPI dispatch with host mocks."""
import subprocess
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--geometry', action='store_true')
    args = parser.parse_args()
    compiler = Path('D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe')
    stem = 'lcd_geometry' if args.geometry else 'lcd_probe'
    output = ROOT/('artifacts/host-check/'+stem+'.exe')
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-DT3P_PROBE_HOST', *(['-DT3P_GEOMETRY_HOST'] if args.geometry else []),
                    str(ROOT/'native'/(stem+'.c')),
                    str(ROOT/'tests/lcd_probe.c'), '-o', str(output)], check=True)
    subprocess.run([str(output)], check=True)

if __name__ == '__main__': main()
