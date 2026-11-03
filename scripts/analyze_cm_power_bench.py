"""Read-only analysis of an explicitly identified power-edge capture.

Clock/data channel roles can be explicitly selected. These roles describe
observed waveforms, not independently established USB connector polarity.
Optional CH3 divider midpoint. Raw conversions
must have been checked against instrument ASCII. No smoothing or ACK claims.
"""
import argparse
import json
from pathlib import Path
import numpy as np

def decode(clock,data,dt):
    rise=np.flatnonzero((clock[:-1]<=1.65)&(clock[1:]>1.65))+1
    fall=np.flatnonzero((clock[:-1]>1.65)&(clock[1:]<=1.65))+1
    matches=[]; tolerance=max(2*dt,20e-9)
    for start in range(max(0,len(rise)-15)):
        rr=rise[start:start+16]
        if np.max(np.abs(np.diff(rr)*dt-2315/120e6))>tolerance: continue
        if any(not np.any(fall>x) for x in rr): continue
        ff=np.array([fall[fall>x][0] for x in rr])
        high=(ff-rr)*dt; low=(rr[1:]-ff[:-1])*dt
        if np.max(np.abs(high-1160/120e6))>tolerance: continue
        if np.max(np.abs(low-1155/120e6))>tolerance: continue
        samples=rr+round(1e-6/dt)
        if samples[-1]>=len(data): continue
        bits=''.join(str(int(data[i]>1.65)) for i in samples)
        if bits=='0001011011101111': matches.append((rr,ff,high,low))
    if len(matches)!=1: raise ValueError(f'Expected one fixed key train, found {len(matches)}')
    return matches[0]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--scope-folder',required=True,type=Path)
    p.add_argument('--record',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--figure',required=True,type=Path)
    context=p.add_mutually_exclusive_group(required=True)
    context.add_argument('--off-target',action='store_true')
    context.add_argument('--target-connected',action='store_true')
    p.add_argument('--clock-channel',type=int,choices=[1,2],default=2)
    p.add_argument('--data-channel',type=int,choices=[1,2],default=1)
    a=p.parse_args()
    if a.clock_channel==a.data_channel: p.error('Clock and data must use different channels')
    meta=json.loads((a.scope_folder/'metadata.json').read_text())
    if [m['channel'] for m in meta] not in ([1,2],[1,2,3]): raise ValueError('Wrong channel mapping')
    volts=[]
    dt=meta[0]['xincrement']; origin=meta[0]['xorigin']; reference=meta[0]['xreference']
    for m in meta:
        if not m['conversion_checked_against_ascii'] or m['adc_min_clipped'] or m['adc_max_clipped']:
            raise ValueError('Unverified conversion or clipped acquisition')
        if (m['xincrement'],m['xorigin'],m['xreference'])!=(dt,origin,reference):
            raise ValueError('Channel time axes differ')
        raw=np.fromfile(a.scope_folder/f"ch{m['channel']}.u16le",dtype='<u2')
        if len(raw)!=m['points'] or m['points']!=meta[0]['points']: raise ValueError('Unequal lengths')
        volts.append((raw.astype(float)-m['yorigin']-m['yreference'])*m['yincrement'])
    data=volts[a.data_channel-1]; clock=volts[a.clock_channel-1]
    rr,ff,high,low=decode(clock,data,dt); base=int(rr[0])
    run=json.loads(a.record.read_text()); events=run['events']
    def window(v,begin,end):
        start=base+round(begin*1e-6/dt); stop=base+round(end*1e-6/dt)
        if not 0<=start<stop<=len(v): raise ValueError('Requested window outside capture')
        return v[start:stop]
    biased=window(clock,400,1800)
    # A held-low timeout keeps the bias on for up to8ms. The old fixed2.6ms
    # window is after release only for the normal handoff, not that timeout.
    normal_handoff=any(e.get('event')=='host_started' for e in events)
    released=window(clock,2600,2800) if normal_handoff else None
    lookback_us=max(1500,run['initial'].get('key_delay_us',100)+2000)
    result=dict(T3P_disconnected=a.off_target,T3P_connected=a.target_connected,
        decoded_word='0x16ef',clock_rises=16,
        channels=dict(clock=a.clock_channel,data=a.data_channel,power_midpoint=3 if len(volts)==3 else None),
        physical_usb_polarity_verified=False,
        channel_roles_note='Clock/data are decoded waveform roles; connector USB+/USB- identity requires separate verification',
        sample_interval_ns=dt*1e9,record_duration_ms=len(clock)*dt*1e3,
        high_us=(high*1e6).tolist(),low_us=(low*1e6).tolist(),
        first_rise_to_last_fall_us=float((ff[-1]-rr[0])*dt*1e6),
        bias_median_v=float(np.median(biased)),
        after_bias_release_median_v=float(np.median(released)) if released is not None else None,
        after_bias_release_note='Fixed2.6..2.8ms window only applies to normal handoff; null if no host handoff',
        power_lookback_us=lookback_us,
        controller_key=[e for e in events if e.get('event')=='key_finished'],
        controller_timing=[e for e in events if e.get('event')=='key_start_timing'],
        controller_power_edges=[e for e in events if e.get('event')=='power_edge'],
        controller_bias=[e for e in events if e.get('event')=='biased_line_observation'],
        host_started=any(e.get('event')=='host_started' for e in events),
        rom_identity_seen=any(e.get('event')=='device_mounted' and e.get('rom_identity_match') is True for e in events),
        stock_identity_seen=any(e.get('event')=='device_mounted' and e.get('stock_identity_match') is True for e in events),
        adc_clipped=False,rom_ack_proven=False,target_flash_access=False)
    observed=window(clock,400,2000)
    result['post_key_bias_window']=dict(begin_us=400,end_us=2000,min_v=float(observed.min()),
        max_v=float(observed.max()),median_v=float(np.median(observed)),samples_below1V5=int((observed<1.5).sum()))
    if len(volts)==3:
        power=volts[2]; lower=max(0,base-round(lookback_us*1e-6/dt)); rises={}
        for level in (1.0,2.0):
            edges=np.flatnonzero((power[:-1]<=level)&(power[1:]>level))+1
            edges=edges[(edges>=lower)&(edges<=base)]
            rises[str(level)]=dict(count=len(edges),
                rising_edges_before_first_clock_us=((edges-base)*dt*1e6).tolist(),
                last_crossing_to_clock_us=float((base-edges[-1])*dt*1e6) if len(edges) else None)
        result['divider_midpoint']=dict(thresholds_v=rises,
            pre_key_median_v=float(np.median(window(power,-1000,-500))),
            post_key_median_v=float(np.median(window(power,400,1800))),
            note='Measured midpoint voltage; nominal VBUS is twice it, divider not individually calibrated')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    left=max(0,base-round(lookback_us*1e-6/dt)); right=min(len(clock),base+round(3000e-6/dt))
    time=(np.arange(left,right)-base)*dt*1e6
    fig,axes=plt.subplots(len(volts),1,figsize=(13,2.5*len(volts)),sharex=True)
    labels=[f'CH{n} / '+('clock waveform' if n==a.clock_channel else 'data waveform') for n in (1,2)]
    labels.append('CH3 / divider midpoint')
    for ax,v,label,color in zip(axes,volts,labels,
                               ['#a87300','#007c99','#7d3c98']):
        ax.plot(time,v[left:right],lw=.6,color=color); ax.set_ylabel('V')
        ax.set_title(label,loc='left'); ax.grid(alpha=.25); ax.set_ylim(-.5,4)
    axes[-1].set_xlabel('Time from first key clock rise (us)')
    fig.suptitle(('T3P disconnected' if a.off_target else 'T3P connected')+
                 ': power edge, fixed key and post-key observation')
    fig.tight_layout(); a.figure.parent.mkdir(parents=True,exist_ok=True)
    fig.savefig(a.figure,dpi=130); plt.close(fig)
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
