"""Run the real engine scheduler's fixed-tic benchmark check on the host."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=Path(
        'D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'))
    args = parser.parse_args()
    output = ROOT/'artifacts/host-check'
    output.mkdir(parents=True, exist_ok=True)
    exe = output/'benchmark-tics.exe'
    command = [str(args.compiler), '-O2', '-DDMC_FW', '-DPLAT_HOST', '-DKM_INPUT_AT_FRAME']
    for folder in ('engine/stub/include', 'engine', 'port/data', 'port/game',
                   'port/render', 'platform', 'gen'):
        command += ['-I', str(ROOT/'vendor/dmc100-doom'/folder)]
    command += [str(ROOT/'tests/benchmark_tics.c'),
                str(ROOT/'vendor/dmc100-doom/engine/d_loop.c'), '-o', str(exe)]
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
