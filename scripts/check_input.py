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
    checks = {
        'input_sensors': ['t3p_input.c'],
        'touch_bus': ['t3p_touch.c'],
        'sensor_controls': ['t3p_input.c', 't3p_tilt.c', 't3p_mic.c'],
        'haptics': ['t3p_bsp.c', 't3p_lcd_sequence.c', 't3p_haptics.c'],
        'barometer': ['t3p_baro.c'],
    }
    defines = {'haptics': ['-DT3P_HAPTICS'], 'sensor_controls': ['-DT3P_TILT_TUNING']}
    for test, drivers in checks.items():
        exe=output/f'{test}.exe'
        subprocess.run([str(args.compiler),'-std=c11','-Wall','-Wextra','-Werror','-O2',
                        *defines.get(test, []),
                        '-I',str(ROOT/'native'),*(str(ROOT/'native'/driver) for driver in drivers),
                        str(ROOT/'native/t3p_sensors.c'),str(ROOT/f'tests/{test}.c'),
                        '-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


if __name__=='__main__':
    main()
