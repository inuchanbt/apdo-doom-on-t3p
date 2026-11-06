"""Check that first-person weapon sprites reach the bottom of the 3D view.

Use a host library built with --weapon-view-y and the normal-clock presenter.
"""
import argparse
import ctypes as C
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROW_BYTES = 320 * 2
PANEL_HEIGHT = 240


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library', type=Path)
    parser.add_argument('--view-height', type=int, choices=(200, 208), default=200)
    args = parser.parse_args()
    dll = C.CDLL(str(args.library.resolve()))
    dll.km_host_init.argtypes = [C.c_void_p, C.c_uint32]
    dll.km_host_step_ms.argtypes = [C.c_uint32, C.c_uint32]
    dll.km_host_screen.restype = C.c_void_p
    dll.km_host_error.restype = C.c_char_p
    data = (ROOT / 'artifacts/game-lowres/E1M1-all-raw.t3pg').read_bytes()
    assert dll.km_host_init(data, len(data)) == 0, dll.km_host_error()

    def step(fire=0):
        assert dll.km_host_step_ms(fire, 29) == 0, dll.km_host_error()

    def check(name):
        dll.km_host_redraw()
        before = C.string_at(dll.km_host_screen(), ROW_BYTES * PANEL_HEIGHT)
        dll.km_host_redraw_without_psprites()
        after = C.string_at(dll.km_host_screen(), len(before))
        view_end = args.view_height * ROW_BYTES
        assert before[view_end:] == after[view_end:], (name, 'weapon leaked into the HUD')
        bottom = view_end - ROW_BYTES
        assert before[bottom:view_end] != after[bottom:view_end], (name, 'floor gap under weapon')
        print(f'{name}: reaches row {args.view_height - 1}, HUD pixels match')

    for _ in range(50):
        step()
    assert dll.km_host_pistol_ammo() == 50 and dll.km_host_weapon_flags() == 0
    check('Idle pistol')
    for _ in range(30):
        step(fire=1)
        if dll.km_host_weapon_flags() & 2:
            break
    assert dll.km_host_weapon_flags() & 2, 'No muzzle flash'
    check('Pistol with muzzle flash')

    for _ in range(1500):
        step(fire=1)
        if dll.km_host_weapon_flags() & 1:
            break
    assert dll.km_host_pistol_ammo() == 0 and dll.km_host_weapon_flags() & 1
    for _ in range(30):
        step()
    check('Fist after ammo runs out')
    for _ in range(30):
        step(fire=1)
        if dll.km_host_weapon_flags() & 4:
            break
    assert dll.km_host_weapon_flags() & 4, 'No punch'
    check('Punch')
    assert dll.km_host_finish() == 0, dll.km_host_error()


if __name__ == '__main__':
    main()
