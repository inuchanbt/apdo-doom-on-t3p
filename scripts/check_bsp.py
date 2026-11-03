"""Host-check the actual board register backend with an independent DBI decoder."""
from pathlib import Path
import subprocess
from generate_lcd_sequence import generate
ROOT=Path(__file__).resolve().parents[1]
def main():
    generate()
    out=ROOT/'artifacts/host-check/bsp.exe'
    compiler='D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'
    subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',
        '-DT3P_BSP_HOST','-DT3P_BOOT_HOST','-I',str(ROOT/'native'),
        *[str(ROOT/p) for p in ['native/t3p_bsp.c','native/t3p_lcd_sequence.c',
                                'native/t3p_boot.c','native/t3p_display.c','tests/bsp.c']],
        '-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
if __name__=='__main__':main()
