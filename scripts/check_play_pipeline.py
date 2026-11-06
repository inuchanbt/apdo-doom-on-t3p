"""Compare serial/overlap gameplay on an injected, variable millisecond clock.

The host DMA deliberately retains its source across delayed polls. This checks
gameplay/image equivalence and ownership, not T3P speed or hardware endurance.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def worker(args):
    dll=C.CDLL(str(args.library.resolve()))
    dll.km_host_init.argtypes=[C.c_void_p,C.c_uint32]
    dll.km_host_step_ms.argtypes=[C.c_uint32,C.c_uint32]
    dll.km_host_state.argtypes=[C.POINTER(C.c_uint32)]
    dll.km_host_present_stats.argtypes=[C.POINTER(C.c_uint32)]
    dll.km_host_clock_ms.restype=C.c_uint32
    dll.km_host_error.restype=C.c_char_p
    dll.km_host_indexed_frame.restype=C.POINTER(C.c_uint8)
    dll.km_host_screen.restype=C.POINTER(C.c_uint16)
    dll.km_host_panel.restype=C.POINTER(C.c_uint16)
    dll.km_host_completed_panel.restype=C.POINTER(C.c_uint16)
    retained=C.create_string_buffer((ROOT/'artifacts/game-lowres/E1M1-all-raw.t3pg').read_bytes())
    assert dll.km_host_init(retained,len(retained)-1)==0,dll.km_host_error()
    state=(C.c_uint32*9)()
    values=(C.c_uint32*6)()
    initial_ammo=dll.km_host_pistol_ammo()
    snapshots=[]
    committed={0:C.string_at(dll.km_host_screen(),320*240*2)}
    last_submitted=last_service=last_tic=0
    multiple_tic_frames=no_draw_frames=0

    def step(keys,elapsed):
        nonlocal last_submitted,last_service,last_tic,multiple_tic_frames,no_draw_frames
        frame=len(snapshots)
        before=dll.km_host_clock_ms()
        assert dll.km_host_step_ms(keys,elapsed)==0,(frame,dll.km_host_error())
        now=dll.km_host_clock_ms()
        assert now>=before+elapsed,(frame,'Elapsed clock was not used')
        dll.km_host_state(state)
        # Normal DOOM catches up several tics, capped by its five-tic queue.
        delta=state[0]-last_tic
        assert 0<=delta<=5,(frame,list(state))
        multiple_tic_frames+=delta>1
        last_tic=state[0]
        screen=C.string_at(dll.km_host_screen(),320*240*2)
        pixels=C.string_at(dll.km_host_indexed_frame(),14400)
        dll.km_host_present_stats(values)
        submitted,completed=values[0],values[1]
        assert submitted-last_submitted in (0,1),(frame,'Unexpected commit count')
        if submitted!=last_submitted:
            committed[submitted]=screen
        else:
            no_draw_frames+=1
            assert screen==committed[submitted],(frame,'Idle frame changed the image')
            assert values[4]>last_service,(frame,'Idle loop did not service DMA')
        assert submitted-completed in (0,1),(frame,'Completion order')
        if args.worker=='serial':
            assert submitted==completed
        actual=C.string_at(dll.km_host_completed_panel(),320*240*2)
        assert actual==committed[completed],(frame,'Completed GRAM differs from its committed image')
        last_submitted,last_service=submitted,values[4]
        snapshots.append(dict(frame=frame,elapsed_ms=elapsed,clock_ms=now,state=list(state),
                              pistol_ammo=dll.km_host_pistol_ammo(),
                              indexed_sha256=sha(pixels),rgb565_sha256=sha(screen)))

    # Include time intervals shorter and longer than a 35 Hz game tic; the
    # five-tic catch-up limit is existing engine behavior, not a new FPS cap.
    intervals=(1,17,29,41,83,145,200)
    for i in range(180):
        step(0,intervals[i%len(intervals)])
    for i in range(40):
        step(1 if i<20 or i%10<5 else 0,145)
    assert 0<=dll.km_host_pistol_ammo()<initial_ammo,'Fire input did not consume ammunition'
    fire_ammo=dll.km_host_pistol_ammo()
    before_position=list(state)[1:3]
    for i in range(20):
        step(2,83)
    assert list(state)[1:3]!=before_position,'Forward input did not move the player'
    before_angle=state[3]
    for i in range(20):
        step(8,83)
    assert state[3]!=before_angle,'Turn input did not change the angle'
    for i,keys in enumerate((32,0,128,0,64,0,32,0)):
        step(keys,83)
        if i==1:
            assert state[7]&1,'Menu did not open'
    assert not state[7]&1,'Menu did not return to play'
    # Pass the former 40-frame stop many times, including further fire input.
    for i in range(400):
        step(1 if 100<=i<112 or 300<=i<312 else 0,145)
    assert multiple_tic_frames>400,'Still using a fixed one-tic benchmark clock'
    assert no_draw_frames>0,'No idle/menu path was exercised'
    assert dll.km_host_finish()==0,dll.km_host_error()
    assert C.string_at(dll.km_host_panel(),320*240*2)==committed[last_submitted]
    dll.km_host_present_stats(values)
    assert values[0]==values[1] and values[3]==0 and values[2]>=values[1]*15
    result=dict(mode=args.worker,steps=len(snapshots),clock_ms=dll.km_host_clock_ms(),
                final_tic=state[0],multiple_tic_frames=multiple_tic_frames,
                no_draw_frames=no_draw_frames,ammo_before=initial_ammo,ammo_after_fire=fire_ammo,
                snapshots=snapshots,present_stats=dict(submitted=values[0],completed=values[1],
                    retained_source_checks=values[2],pending=values[3],service_calls=values[4]),
                library_sha256=sha(args.library.read_bytes()))
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(args.worker,'PASS',len(snapshots),'steps;',state[0],'game tics;',values[2],'retained-source checks')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker',choices=['serial','overlap'])
    parser.add_argument('--library',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.worker:
        worker(args)
        return
    results=[]
    for mode in ('serial','overlap'):
        output=ROOT/'artifacts/play-pipeline-engine-check'/mode
        subprocess.run([sys.executable,__file__,'--worker',mode,'--library',str(
            ROOT/f'artifacts/host-present-play-{mode}/t3pdoom-test.dll'),
            '--output',str(output)],check=True)
        results.append(json.loads((output/'result.json').read_text('utf-8')))
    serial,overlap=results
    assert serial['snapshots']==overlap['snapshots'],'Serial and overlapped real-clock gameplay differ'
    result=dict(status='HOST NORMAL-CLOCK PLAY CHECKS PASS; V49 device trial pending',
                steps_per_variant=serial['steps'],injected_clock_ms=serial['clock_ms'],
                final_game_tic=serial['final_tic'],multiple_tic_frames=serial['multiple_tic_frames'],
                no_draw_frames=serial['no_draw_frames'],
                ammo_before=serial['ammo_before'],ammo_after_fire=serial['ammo_after_fire'],
                all_indexed_RGB565_frames_and_game_state_identical=True,
                fire_move_turn_menu_and_continuation_past_frame_40=True,
                deferred_ownership_and_completed_gram_checked_each_step=True,
                final_dma_drain_checked=True,
                serial=serial['present_stats'],overlap=overlap['present_stats'],
                host_library_sha256={r['mode']:r['library_sha256'] for r in results},
                source_sha256={p:sha((ROOT/p).read_bytes()) for p in (
                    'native/t3p_present.c','native/t3p_present.h','native/host_harness.c',
                    'native/doom_bridge.c','tests/host_play_probe.c','scripts/build_engine.py',
                    'scripts/check_play_pipeline.py')},
                limitations=['No physical button, sensor or device execution in this host check.',
                             'Injected game clock and poll delays are not a hardware FPS or endurance measurement.',
                             'Movement/menu checks use synthetic input; only the known PA11 fire key is wired in V49.'])
    (ROOT/'analysis/native-doom-v49-host-check.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('PASS: serial/overlap normal-clock gameplay, pixels and ammo agree for',serial['steps'],'steps')


if __name__=='__main__':
    main()
