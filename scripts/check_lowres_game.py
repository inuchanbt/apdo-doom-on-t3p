"""Compare complete indexed engine frames for the low-resolution asset pair.

Each engine runs in a fresh host process. Host timings are deliberately not
reported as T3P performance. The baseline fingerprint validates the indexed
host harness against the already measured V43/V44 scene.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def worker(args):
    dll = C.CDLL(str(args.library.resolve()))
    dll.km_host_init.argtypes = [C.c_void_p,C.c_uint32]
    dll.km_host_step.argtypes = [C.c_uint32]
    dll.km_host_state.argtypes = [C.POINTER(C.c_uint32)]
    dll.km_host_game_stats.argtypes = [C.POINTER(C.c_uint32)]
    dll.km_host_error.restype = C.c_char_p
    dll.km_host_indexed_frame.restype = C.POINTER(C.c_uint8)
    dll.km_host_screen.restype = C.POINTER(C.c_uint16)
    dll.km_host_panel.restype = C.POINTER(C.c_uint16)
    retained = C.create_string_buffer(args.packed.read_bytes())
    assert dll.km_host_init(retained, len(retained)-1)==0, dll.km_host_error()
    state = (C.c_uint32*9)()
    stats = (C.c_uint32*3)()
    snapshots = []
    sequence = 2166136261
    previous_screen = C.string_at(dll.km_host_screen(),320*240*2)
    submitted_images = {0:previous_screen}
    last_submitted = last_service_calls = 0
    if args.present_mode:
        dll.km_host_completed_panel.restype = C.POINTER(C.c_uint16)
        dll.km_host_present_stats.argtypes = [C.POINTER(C.c_uint32)]
    args.output.mkdir(parents=True,exist_ok=True)
    def step(keys, frame):
        nonlocal previous_screen, last_submitted, last_service_calls
        assert dll.km_host_step(keys)==0, (frame,dll.km_host_error())
        dll.km_host_state(state)
        assert state[0]==frame+1, (frame,list(state))
        pixels = C.string_at(dll.km_host_indexed_frame(),14400)
        screen = C.string_at(dll.km_host_screen(),320*240*2)
        panel = C.string_at(dll.km_host_panel(),320*320*2)
        if args.present_mode:
            values=(C.c_uint32*6)()
            dll.km_host_present_stats(values)
            submitted,completed=values[0],values[1]
            assert submitted-last_submitted in (0,1), (frame,'Unexpected commit count')
            if submitted!=last_submitted:
                submitted_images[submitted]=screen
            else:
                assert screen==submitted_images[submitted], (frame,'No-draw frame changed the committed image')
                assert values[4]>last_service_calls, (frame,'Idle frame did not service the pending transfer')
            assert submitted-completed in (0,1), (frame,'Invalid completion order')
            if args.present_mode=='serial':
                assert submitted==completed
            completed = C.string_at(dll.km_host_completed_panel(),320*240*2)
            expected = submitted_images[values[1]]
            assert completed==expected, (frame,'Deferred GRAM disagrees with the completed image')
            last_submitted,last_service_calls=submitted,values[4]
        else:
            assert screen==panel[:320*240*2], 'Virtual GRAM disagrees with the completed indexed frame'
        previous_screen = screen
        snapshots.append(dict(frame=frame,tic=state[0],state=list(state),indexed_sha256=hashlib.sha256(pixels).hexdigest(),
                              rgb565_sha256=hashlib.sha256(screen).hexdigest()))
        if frame in (39,43,139):
            (args.output/f'frame-{frame:03}.rgb565').write_bytes(screen)
        return pixels
    for frame in range(140):
        # Same no-input 10 + 30 frames as the device, then fire, move and turn.
        keys = 0 if frame<40 else 1 if frame<60 else 2 if frame<100 else (2|8)
        pixels = step(keys,frame)
        if 10<=frame<40:
            for byte in pixels:
                sequence = ((sequence^byte)*16777619)&0xffffffff
    assert snapshots[39]['state'][1:3]!=snapshots[99]['state'][1:3], 'Forward input did not move the player'
    assert snapshots[99]['state'][3]!=snapshots[139]['state'][3], 'Turn input did not change the angle'
    assert snapshots[39]['indexed_sha256']!=snapshots[43]['indexed_sha256'], 'Fire workload did not change the image'
    # HELP1 has no main-menu entry in this engine; exercise menu rendering,
    # options and a return to play with the retained UI assets.
    for frame,keys in enumerate((32,0,128,0,64,0,32,0),140):
        step(keys,frame)
        if frame==141:
            assert snapshots[-1]['state'][7]&1, 'Menu did not open'
    assert not snapshots[-1]['state'][7]&1, 'Menu did not return to play'
    dll.km_host_game_stats(stats)
    present_stats = None
    if args.present_mode:
        assert dll.km_host_finish()==0, dll.km_host_error()
        assert C.string_at(dll.km_host_panel(),320*240*2)==previous_screen
        values=(C.c_uint32*6)()
        dll.km_host_present_stats(values)
        assert values[0]==values[1] and values[2]>0 and values[3]==0
        present_stats=dict(submitted=values[0],completed=values[1],retained_source_checks=values[2],
                           pending=values[3],service_calls=values[4],state_bytes_host=values[5])
    result=dict(status='HOST ENGINE ONLY; no device speed claim',label=args.worker,
                frames=len(snapshots),benchmark_final_tic=40,
                benchmark_sequence_hash=f'{sequence:08X}',snapshots=snapshots,
                cache_hits=stats[0],cache_misses=stats[1],reader_state_bytes_host=stats[2],
                completed_frame_matches_virtual_gram=True,
                movement_turn_fire_workload_and_menu=True)
    if present_stats is not None:
        result['present_mode']=args.present_mode
        result['present_stats']=present_stats
    (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    # This is a renderer preview, not a photograph of the device.
    from PIL import Image
    screen=(args.output/'frame-039.rgb565').read_bytes()
    rgb=bytearray()
    for y in range(240):
        for x in range(240):
            at=(320*y+x)*2
            color=int.from_bytes(screen[at:at+2],'little')
            r,g,b=(color>>11)&31,(color>>5)&63,color&31
            rgb+=bytes(((r*255+15)//31,(g*255+31)//63,(b*255+15)//31))
    Image.frombytes('RGB',(240,240),bytes(rgb)).resize((720,720),Image.Resampling.NEAREST).save(args.output/'host-preview.png')
    print(args.worker, 'PASS',len(snapshots),'frames, 40-tic sequence',f'{sequence:08X}',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker')
    parser.add_argument('--library',type=Path)
    parser.add_argument('--packed',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--present-mode',choices=['serial','overlap'])
    args=parser.parse_args()
    if args.worker:
        worker(args)
        return
    cases=[
        ('baseline','host-lowres-deflate','native-doom-v44/E1M1-2k.t3pg'),
        ('lowres-deflate','host-lowres-deflate','game-lowres/E1M1-deflate.t3pg'),
        ('lowres-raw','host-lowres-raw','game-lowres/E1M1-all-raw.t3pg'),
    ]
    results=[]
    for label,folder,packed in cases:
        output=ROOT/'artifacts/lowres-engine-check'/label
        subprocess.run([sys.executable,__file__,'--worker',label,
                        '--library',str(ROOT/'artifacts'/folder/'t3pdoom-test.dll'),
                        '--packed',str(ROOT/'artifacts'/packed),'--output',str(output)],check=True)
        results.append(json.loads((output/'result.json').read_text(encoding='utf-8')))
    baseline,compressed,raw=results
    assert baseline['benchmark_sequence_hash']=='C269CBCE', 'Host indexed harness does not match the device baseline'
    assert compressed['snapshots']==raw['snapshots'], 'Storage modes disagree on pixels or gameplay state'
    assert compressed['benchmark_sequence_hash']==raw['benchmark_sequence_hash']
    assert raw['cache_hits']==raw['cache_misses']==0 and raw['reader_state_bytes_host']<1024
    public=dict(status='HOST CHECKS PASS; V45/V46 device boot, quality and performance untested',
                existing_v44_scene_fingerprint=baseline['benchmark_sequence_hash'],
                expected_v45_v46_scene_fingerprint=raw['benchmark_sequence_hash'],
                expected_final_game_tic=40,expected_benchmark_frames=30,
                compressed_and_raw_identical_frames_and_game_state=len(raw['snapshots']),
                virtual_gram_checked_after_every_frame=True,
                movement_turn_fire_workload_and_menu=True,
                host_reader_state_bytes=dict(compressed=compressed['reader_state_bytes_host'],raw_only=raw['reader_state_bytes_host']),
                limitations=['No on-device execution, LCD-transfer timing or frame-rate measurement.',
                             'V44 uses different texture/sprite detail; use V45 versus V46 to isolate storage changes.',
                             'Input checks use host key injection; physical sensor and extra-button integration is unchanged.'])
    (ROOT/'analysis/native-doom-v45-v46-host-check.json').write_text(json.dumps(public,indent=2)+'\n',encoding='utf-8')
    print('PASS: existing device fingerprint reproduced; compressed/raw frames and state match for',len(raw['snapshots']),'steps')


if __name__=='__main__':
    main()
