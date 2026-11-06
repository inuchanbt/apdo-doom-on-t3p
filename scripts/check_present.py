"""Check deferred frame ownership and the real DBI scheduler without USB."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=Path('D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'))
    args = parser.parse_args()
    output = ROOT/'artifacts/host-check'
    output.mkdir(parents=True, exist_ok=True)
    for name, sources, flags in [
        ('present', ['native/t3p_present.c','tests/present.c'], ['-DT3P_STARTUP_BANNER']),
        ('bsp-dma', ['native/t3p_bsp.c','native/t3p_lcd_sequence.c','tests/bsp_dma.c'],
         ['-DT3P_BSP_HOST','-DT3P_DBI_DMA','-DT3P_DBI_ASYNC','-DT3P_DBI_ACTIVE_LINE','-DT3P_FRAME_PRESENTER','-DT3P_DBI_CLOCK_DIV=2']),
    ]:
        exe = output/(name+'.exe')
        subprocess.run([str(args.compiler),'-std=c11','-O2','-Wall','-Wextra','-Werror',
                        '-I',str(ROOT/'native'),*flags,*[str(ROOT/p) for p in sources],'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    main()
