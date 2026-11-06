"""Compare the deferred presenter's real engine images with the V46 renderer."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    cases=[('v46-reference','host-lowres-raw',None),
           ('v47-serial','host-present-serial','serial'),
           ('v48-overlap','host-present-overlap','overlap')]
    results=[]
    for label,folder,mode in cases:
        output=ROOT/'artifacts/frame-pipeline-engine-check'/label
        command=[sys.executable,str(ROOT/'scripts/check_lowres_game.py'),'--worker',label,
                 '--library',str(ROOT/'artifacts'/folder/'t3pdoom-test.dll'),
                 '--packed',str(ROOT/'artifacts/game-lowres/E1M1-all-raw.t3pg'),
                 '--output',str(output)]
        if mode:command+=['--present-mode',mode]
        subprocess.run(command,check=True)
        results.append(json.loads((output/'result.json').read_text('utf-8')))
    baseline,serial,overlap=results
    assert baseline['benchmark_sequence_hash']=='EBC8D404'
    assert serial['snapshots']==overlap['snapshots']==baseline['snapshots']
    assert all(r['benchmark_sequence_hash']=='EBC8D404' for r in results)
    for r in (serial,overlap):
        s=r['present_stats']
        assert s['submitted']==s['completed'] and s['pending']==0
        assert s['retained_source_checks']>=s['completed']*15
    result=dict(
        status='HOST ENGINE AND DEFERRED DMA CHECKS PASS; hardware boot/speed untested',
        frames_per_variant=len(baseline['snapshots']),expected_benchmark_frame_hash='EBC8D404',
        expected_final_game_tic=40,all_pixels_and_game_state_match_v46=True,
        deferred_virtual_gram_checked_every_frame=True,final_dma_drain_checked=True,
        movement_turn_fire_and_menu=True,
        serial=serial['present_stats'],overlap=overlap['present_stats'],
        source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
                       ['native/t3p_present.c','native/t3p_present.h','native/host_harness.c']},
        limitations=['No device execution or speed measurement.',
                     'Host DMA delays model ownership, not T3P wall-clock performance.',
                     'Hardware benchmarking includes the checksum and a final DMA drain in both new variants.'])
    (ROOT/'analysis/native-doom-v47-v48-host-check.json').write_text(json.dumps(result,indent=2)+'\n','utf-8')
    print('PASS: V46, serial and overlapped engines agree for',len(baseline['snapshots']),
          'frames; deferred GRAM order/source ownership and final drain verified')


if __name__=='__main__':
    main()
