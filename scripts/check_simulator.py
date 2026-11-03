"""Run DOOM on Windows through the T3P input mixer and compressed asset reader.

This is a host simulation, not evidence that the physical T3P runs DOOM.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import time

ROOT=Path(__file__).resolve().parents[1]


class Sample(C.Structure):
    _fields_=[('now_ms',C.c_uint32),('tilt_x',C.c_int16),('tilt_y',C.c_int16),
              ('touch_x',C.c_uint16),('touch_y',C.c_uint16),('buttons',C.c_uint8),
              ('buttons_valid',C.c_uint8),('tilt_valid',C.c_uint8),('touch_valid',C.c_uint8),
              ('touch_down',C.c_uint8),('menu',C.c_uint8)]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--panel',type=int,choices=[240,320],default=240,
                        help='Virtual panel: default240 matches observed T3P visible area;320 is a comparison profile')
    parser.add_argument('--transport',choices=['commands','native-rgb'],default='commands',
                        help='native-rgb exercises the stock-compatible RGB565 rectangle contract, with host callbacks')
    args=parser.parse_args()
    output=ROOT/'artifacts/host-engine'
    dll=C.CDLL(str(output/'t3pdoom-test.dll'))
    dll.km_host_display_transport.argtypes=[C.c_uint]
    assert dll.km_host_display_transport(args.transport=='native-rgb')==0
    if args.transport=='native-rgb':
        output=output/'native-rgb'
        output.mkdir(exist_ok=True)
    dll.km_host_init.argtypes=[C.c_void_p,C.c_uint32]
    dll.km_host_sample.argtypes=[C.POINTER(Sample)]
    dll.km_host_step.argtypes=[C.c_uint32]
    dll.km_host_error.restype=C.c_char_p
    dll.km_host_screen.restype=C.POINTER(C.c_uint16)
    dll.km_host_state.argtypes=[C.POINTER(C.c_uint32)]
    dll.km_host_game_stats.argtypes=[C.POINTER(C.c_uint32)]
    dll.km_host_panel_config.argtypes=[C.c_uint]
    dll.km_host_panel.restype=C.POINTER(C.c_uint16)
    dll.km_host_display_stats.argtypes=[C.POINTER(C.c_uint32)]
    assert dll.km_host_panel_config(args.panel)==0
    pack=(ROOT/'artifacts/game/E1M1.t3pg').read_bytes()
    retained=C.create_string_buffer(pack)
    assert dll.km_host_init(retained,len(pack))==0,dll.km_host_error()
    assert dll.km_host_check_projection()==0
    state=(C.c_uint32*9)()
    stats=(C.c_uint32*3)()
    sample=Sample(buttons_valid=1,tilt_valid=1)
    frames=[]
    states={}
    started=time.perf_counter()
    def check_display():
        direct=C.string_at(dll.km_host_screen(),320*240*2)
        panel=C.string_at(dll.km_host_panel(),320*320*2)
        expected=bytearray(320*320*2)
        margin=(args.panel-240)//2
        for y in range(240):
            at=((y+margin)*320+margin)*2
            expected[at:at+480]=direct[y*640:y*640+480]
        assert panel==expected, 'Controller stream differs from engine pixels or changed the border'
    check_display()
    for frame in range(140):
        sample.tilt_y=-600 if 35<=frame<70 else 0
        sample.tilt_x=600 if 70<=frame<100 else 0
        sample.buttons=1 if 100<=frame<125 else 0
        assert dll.km_host_sample(C.byref(sample))==0,(frame,dll.km_host_error())
        check_display()
        if frame in [0,34,69,99,124,139]:
            pixels=C.string_at(dll.km_host_screen(),320*240*2)
            (output/f'frame-{frame:03}.rgb565').write_bytes(pixels)
            dll.km_host_state(state)
            states[frame]=list(state)
            frames.append(dict(frame=frame,sha256=hashlib.sha256(pixels).hexdigest(),state=list(state)))
        if frame==99:
            (output/f'panel-{args.panel}-099.rgb565').write_bytes(
                C.string_at(dll.km_host_panel(),320*320*2))
    assert len({entry['sha256'] for entry in frames})>=4
    assert states[34][1:3]!=states[69][1:3], 'tilt did not move player'
    assert states[69][3]!=states[99][3], 'tilt did not turn player'
    # Exercise the two-button menu chord through the mixer, not raw key injection.
    sample.buttons=3; sample.tilt_x=sample.tilt_y=600
    for _ in range(30):
        assert dll.km_host_sample(C.byref(sample))==0,dll.km_host_error()
        check_display()
    dll.km_host_state(state)
    assert state[7]&1, 'two-button chord failed to open menu'
    before=list(state[1:4])
    for _ in range(5):
        assert dll.km_host_sample(C.byref(sample))==0
        check_display()
    dll.km_host_state(state)
    assert list(state[1:4])==before, 'tilt moved player in menu'
    dll.km_host_game_stats(stats)
    display_stats=(C.c_uint32*3)()
    dll.km_host_display_stats(display_stats)
    manifest=dict(status='WINDOWS HOST DOOM SIMULATION ONLY; DOOM not executed on T3P',
                  frames=frames,input_path='normalized tilt + two buttons through t3p_input.c',
                  game_path='T3PGAME1 + native t3p_game.c + tinf, 4KiB bounded LRU page cache',
                  cache_hits=stats[0],cache_misses=stats[1],cache_state_bytes_host_abi=stats[2],
                  timed_175_steps_seconds=round(time.perf_counter()-started,3),
                  display=dict(virtual_panel_size=args.panel,viewport_size=240,
                      matches_observed_visible_area=args.panel==240,
                      physical_geometry_evidence='analysis/lcd-geometry-physical-success.json',
                      path=('8-column staging -> native RGB565 rectangle -> virtual GRAM'
                            if args.transport=='native-rgb' else
                            '8-column staging -> 2A/2B/2C -> virtual controller GRAM'),
                      transport=args.transport,
                      every_step_pixel_and_border_comparison=True,
                      windows=display_stats[0],pixel_bytes=display_stats[1],
                      state_bytes_host_abi=display_stats[2]))
    (output/'simulator-check.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    (output/f'simulator-panel-{args.panel}.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    # The intermediate renderer surface is320x240; the visible game area is240x240.
    # Output below is a host rendering, never a photograph of device DOOM.
    from PIL import Image
    pixels=(output/'frame-099.rgb565').read_bytes()
    rgb=bytearray()
    for offset in range(0,len(pixels),2):
        color=int.from_bytes(pixels[offset:offset+2],'little')
        r,g,b=(color>>11)&31,(color>>5)&63,color&31
        rgb+=bytes(((r*255+15)//31,(g*255+31)//63,(b*255+15)//31))
    Image.frombytes('RGB',(320,240),bytes(rgb)).resize((640,480),Image.Resampling.NEAREST).save(output/'windows-simulator.png')
    pixels=(output/f'panel-{args.panel}-099.rgb565').read_bytes()
    rgb=bytearray()
    for offset in range(0,len(pixels),2):
        color=int.from_bytes(pixels[offset:offset+2],'little')
        r,g,b=(color>>11)&31,(color>>5)&63,color&31
        rgb+=bytes(((r*255+15)//31,(g*255+31)//63,(b*255+15)//31))
    Image.frombytes('RGB',(320,320),bytes(rgb)).crop((0,0,args.panel,args.panel)).resize(
        (args.panel*2,args.panel*2),Image.Resampling.NEAREST).save(output/f'virtual-panel-{args.panel}.png')
    print(f'PASS: compressed-game startup, 140 rendered frames, tilt movement/turn, fire and 2-button menu; {stats[0]} cache hits/{stats[1]} misses')
    print(f'PASS: {args.panel}px virtual panel protocol, all pixels and border after every step; {display_stats[0]} windows')
    print('Preview:',output/'windows-simulator.png')


if __name__=='__main__':
    main()
