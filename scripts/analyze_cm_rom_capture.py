"""Find the fixed key train in a physical two-channel T3P capture.

Reads existing raw files only. Does not treat weak-bias low levels as ACK.
CH1 is D- data and CH2 is D+ clock. No edge smoothing or channel guessing.
"""
import argparse
import json
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    group=p.add_mutually_exclusive_group(required=True)
    group.add_argument('--attempt',type=int)
    group.add_argument('--bias-bench',action='store_true')
    a=p.parse_args()
    name='bias-bench' if a.bias_bench else f'target{a.attempt}'
    folder=ROOT/f'local/scope-captures/{name}-raw'
    meta=json.loads((folder/'metadata.json').read_text())
    assert [m['channel'] for m in meta]==[1,2]
    volts=[]
    for m in meta:
        assert m['conversion_checked_against_ascii']
        raw=np.fromfile(folder/f"ch{m['channel']}.u16le",dtype='<u2')
        assert len(raw)==m['points'] and not m['adc_min_clipped'] and not m['adc_max_clipped']
        volts.append((raw.astype(float)-m['yorigin']-m['yreference'])*m['yincrement'])
    data,clock=volts
    dt=meta[0]['xincrement']; origin=meta[0]['xorigin']
    assert meta[1]['xincrement']==dt and meta[1]['xorigin']==origin
    rise=np.flatnonzero((clock[:-1]<=1.65)&(clock[1:]>1.65))+1
    fall=np.flatnonzero((clock[:-1]>1.65)&(clock[1:]<=1.65))+1
    frames=[]
    tolerance=max(2*dt,20e-9)
    for start in range(max(0,len(rise)-15)):
        rr=rise[start:start+16]
        if np.max(np.abs(np.diff(rr)*dt-2315/120e6))>tolerance: continue
        if any(not np.any(fall>x) for x in rr): continue
        ff=np.array([fall[fall>x][0] for x in rr])
        high=(ff-rr)*dt; low=(rr[1:]-ff[:-1])*dt
        if np.max(np.abs(high-1160/120e6))>tolerance: continue
        if np.max(np.abs(low-1155/120e6))>tolerance: continue
        bits=''.join(str(int(data[i+round(1e-6/dt)]>1.65)) for i in rr)
        if bits!='0001011011101111': continue
        frames.append((rr,ff,bits,high,low))
    assert len(frames)==1,f'Expected one unambiguous fixed key train, found {len(frames)}'
    rr,ff,bits,high,low=frames[0]; base=int(rr[0])
    def median(v,begin,end):
        x=base+round(begin*1e-6/dt); y=base+round(end*1e-6/dt)
        assert 0<=x<y<=len(v)
        return float(np.median(v[x:y]))
    windows=[(-200,-180),(-70,-50),(310,340),(400,450),(900,1000),(1500,1600),(2500,2600)]
    record='local/cm-bias-bench-handoff.json' if a.bias_bench else f'local/cm-rom-target-attempt{a.attempt}.json'
    run=json.loads((ROOT/record).read_text())
    rom_seen=any(e.get('rom_identity_match') is True for e in run['events'])
    stock_seen=any(e.get('stock_identity_match') is True for e in run['events'])
    result=dict(attempt=a.attempt,sample_interval_ns=dt*1e9,record_duration_ms=len(clock)*dt*1000,
        channels=dict(data='CH1 D-',clock='CH2 D+'),decoded_bits=bits,decoded_word='0x16ef',
        clock_rises=16,high_us=[float(x) for x in high*1e6],low_us=[float(x) for x in low*1e6],
        first_clock_rise_to_last_fall_us=float((ff[-1]-rr[0])*dt*1e6),
        plateau_windows=[dict(begin_us=b,end_us=e,data_v=median(data,b,e),clock_v=median(clock,b,e)) for b,e in windows],
        adc_clipped=False,rom_ack_proven=False,
        interpretation='Fixed key is present at the connected-device breakout. Low after release alone cannot prove ROM ACK under this bias.',
        rom_identity_seen=rom_seen,stock_identity_seen=stock_seen,target_flash_access=False)
    biased=[e for e in run['events'] if e.get('event')=='biased_line_observation']
    if biased:
        result['controller_biased_observation']=biased[-1]
        x=base+round(400e-6/dt); y=base+round(2000e-6/dt)
        sample=clock[x:y]
        result['post_key_bias_window']=dict(begin_us=400,end_us=2000,
            min_v=float(sample.min()),median_v=float(np.median(sample)),
            max_v=float(sample.max()),samples_below_1v5=int((sample<1.5).sum()))
        result['interpretation']='Fixed key decoded. GPIO10 resistor-bias observation and subsequent USB identity are recorded separately; no automatic ROM ACK claim.'
    if a.bias_bench:
        result['T3P_disconnected']=True
        result['interpretation']='Off-target fixed key followed by GPIO10 resistor bias, release and USB host start.'
        result['bias_high_v']=median(clock,400,1800)
        result['bias_released_low_v']=median(clock,2600,2800)
        assert 2.5<result['bias_high_v']<3.6 and abs(result['bias_released_low_v'])<.3
    out=ROOT/('analysis/cm-bias-bench-wave.json' if a.bias_bench else f'analysis/cm-rom-target{a.attempt}-wave.json')
    out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    left=max(0,base-round(250e-6/dt)); right=min(len(clock),base+round((3000 if a.bias_bench else 1800)*1e-6/dt))
    time=(np.arange(left,right)-base)*dt*1e6
    fig,axes=plt.subplots(2,1,figsize=(13,6),sharex=True)
    for ax,v,label,color in zip(axes,volts,['CH1 / D- data','CH2 / D+ clock'],['#a87300','#007c99']):
        ax.plot(time,v[left:right],lw=.6,color=color)
        ax.axhline(1.65,color='gray',lw=.5,ls='--')
        ax.set_ylabel('V'); ax.set_title(label,loc='left'); ax.grid(alpha=.25); ax.set_ylim(-.5,4)
    axes[-1].set_xlabel('Time from first key clock rise (us)')
    identity='ROM candidate' if rom_seen else 'stock HID' if stock_seen else 'not observed'
    fig.suptitle('T3P disconnected: fixed key, resistor bias and release' if a.bias_bench else
                 f'T3P connected: measured 0x16EF; subsequent USB identity: {identity}')
    fig.tight_layout()
    fig.savefig(ROOT/f'artifacts/cm-wave-scope/{name}-key-and-after.png',dpi=130)
    plt.close(fig)
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
