"""Analyze the actual two-channel MHO984 capture, without smoothing edges."""
import json
import argparse
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frame',type=int,choices=[3,4,5,6,7,8],default=3)
    parser.add_argument('--clock-channel',type=int,choices=[1,2],default=1,
                        help='Explicit measured clock channel; raw channel files are unchanged')
    parser.add_argument('--clock-line',choices=['D-','D+'],default='D-',
                        help='Physical USB line carrying the candidate clock')
    args=parser.parse_args()
    frame=args.frame
    folder=ROOT/f'local/scope-captures/frame{frame}-raw'
    metadata=json.loads((folder/'metadata.json').read_text(encoding='utf-8'))
    volts=[]
    for m in metadata:
        assert m['conversion_checked_against_ascii']
        raw=np.fromfile(folder/f"ch{m['channel']}.u16le",dtype='<u2')
        assert len(raw)==m['points']
        volts.append((raw.astype(float)-m['yorigin']-m['yreference'])*m['yincrement'])
    clock_channel=args.clock_channel
    data_channel=3-clock_channel
    volts=[volts[clock_channel-1],volts[data_channel-1]]
    clock,data=volts
    assert len(clock)==len(data)
    dt=metadata[0]['xincrement']; origin=metadata[0]['xorigin']
    assert metadata[1]['xincrement']==dt and metadata[1]['xorigin']==origin
    threshold=1.65
    edges=lambda v:(np.flatnonzero((v[:-1]<=threshold)&(v[1:]>threshold))+1,
                    np.flatnonzero((v[:-1]>threshold)&(v[1:]<=threshold))+1)
    rises,falls=edges(clock); data_rises,data_falls=edges(data)
    assert len(rises)==len(falls)==16,'Not exactly 16 clean clocks; inspect raw data'
    bits=[int(data[i+round(1e-6/dt)]>threshold) for i in rises]
    before_bits=[int(data[i-round(50e-9/dt)]>threshold) for i in rises]
    bit_string=''.join(map(str,bits))
    assert bit_string=='0001011011101111' and before_bits==bits
    high=(falls-rises)*dt
    low=(rises[1:]-falls[:-1])*dt
    all_data_edges=np.sort(np.concatenate([data_rises,data_falls]))
    setup=[]
    for i in range(1,16):
        if bits[i]==bits[i-1]: continue
        candidates=all_data_edges[(all_data_edges<rises[i])&(all_data_edges>rises[i]-round(1e-6/dt))]
        assert len(candidates)==1,'Data transition not clean before clock'
        setup.append(dict(bit_index=i,data_to_clock_ns=float((rises[i]-candidates[0])*dt*1e9)))
    assert np.max(np.abs(high-1160/120e6))<=5e-9
    assert np.max(np.abs(low-1155/120e6))<=5e-9
    assert all(abs(x['data_to_clock_ns']-125)<=5 for x in setup)
    def window_median(v,starts):
        values=[v[i+round(2e-6/dt):i+round(7e-6/dt)] for i in starts]
        return float(np.median(np.concatenate(values)))
    levels=dict(clock_low_v=window_median(clock,falls),clock_high_v=window_median(clock,rises),
                data_low_v=window_median(data,[i for i,b in zip(rises,bits) if not b]),
                data_high_v=window_median(data,[i for i,b in zip(rises,bits) if b]))
    after=data_falls[data_falls>falls[-1]]
    decay_ns=float((after[0]-falls[-1])*dt*1e9) if len(after) else None
    result=dict(scope='Physical MHO984 acquisition at CM USB-C breakout; T3P disconnected',
                points_per_channel=len(clock),sample_interval_ns=dt*1e9,record_duration_us=len(clock)*dt*1e6,
                threshold_v=threshold,clock_rises=16,clock_falls=16,decoded_bits=bit_string,decoded_word='0x16ef',
                bits_stable_50ns_before_and_1us_after_clock=True,
                high_us=dict(min=float(high.min()*1e6),median=float(np.median(high)*1e6),max=float(high.max()*1e6)),
                low_us=dict(min=float(low.min()*1e6),median=float(np.median(low)*1e6),max=float(low.max()*1e6)),
                changed_bit_setup=setup,plateau_levels=levels,
                first_clock_rise_to_last_fall_us=float((falls[-1]-rises[0])*dt*1e6),
                last_data_threshold_decay_after_clock_ns=decay_ns,
                transient_peaks={f"ch{m['channel']}":dict(min_v=m['min_v'],max_v=m['max_v']) for m in metadata},
                adc_clipping=False,digital_pattern_and_nominal_timing_pass=True,
                output_enable_release_ns_physically_verified=False,rom_compatibility_verified=False,
                limits=['Edge peaks/ringing need probe-ground comparison',
                        'Post-release voltage decay includes RC; not a direct measurement of PIO output-enable timing',
                        'Unloaded breakout capture, not T3P-connected ROM reception'])
    assert all(m['adc_min_clipped']==m['adc_max_clipped']==0 for m in metadata)
    output=ROOT/'artifacts/cm-wave-scope'; output.mkdir(exist_ok=True)
    result['capture_ordinal']=frame
    result['signal_channels']=dict(clock=clock_channel,data=data_channel)
    clock_line=args.clock_line
    data_line='D-' if clock_line=='D+' else 'D+'
    result['signal_usb_lines']=dict(clock=clock_line,data=data_line)
    result['ground_configuration']='Extra 20cm GND extension removed' if frame>=4 else 'Extra 20cm GND extension present'
    if frame>=5: result['drive_setting']='2mA, slow slew, read back by CM firmware'
    if frame==6:
        result['breakout_configuration']='New male/female inline breakout; requested lower-row D- CH1, D+ CH2; observed clock CH2/data CH1; male end empty'
    if frame==7:
        result['breakout_configuration']='New male/female inline breakout, T3P removed. User corrected reversed probes: CH1=D-, CH2=D+. Firmware v4 candidate: clock D+, data D-.'
    if frame==8:
        result['breakout_configuration']='New male/female inline breakout, T3P removed. CH1=D-, CH2=D+. One-attach ROM probe v1 BENCH command, followed by descriptor-only host start.'
    (ROOT/f'analysis/cm-wave-physical-frame{frame}.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    # Standalone plots of measured data. No retouching or generated waveform.
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    base=rises[0]
    begin=max(0,base-round(15e-6/dt)); end=min(len(clock),falls[-1]+round(15e-6/dt))
    time=(np.arange(begin,end)-base)*dt*1e6
    fig,axes=plt.subplots(2,1,figsize=(15,6.7),sharex=True)
    for ax,v,color,label in zip(axes,volts,['#a87300','#007c99'],
                                [f'CH{clock_channel} / CM {clock_line} clock',f'CH{data_channel} / CM {data_line} data']):
        ax.plot(time,v[begin:end],color=color,linewidth=0.6)
        ax.axhline(threshold,color='gray',linewidth=0.5,linestyle='--')
        ax.set_ylim(-1.3,4.6); ax.set_ylabel('V'); ax.set_title(label,loc='left',fontsize=11)
        ax.grid(alpha=0.2)
    for i,b in enumerate(bits):
        at=(rises[i]-base)*dt*1e6
        axes[1].text(at+3,3.9,str(b),ha='center',fontsize=11,fontweight='bold')
    axes[-1].set_xlabel('Time from first clock rise (us)')
    axes[-1].set_xlim(time[0],time[-1])
    fig.suptitle('Measured CM scope-only frame: 0x16EF / 16 clocks',fontsize=14)
    fig.tight_layout(); fig.savefig(output/f'measured-frame{frame}.png',dpi=140); plt.close(fig)
    pivot=rises[setup[0]['bit_index']]
    a=pivot-round(350e-9/dt); b=pivot+round(350e-9/dt)
    ns=(np.arange(a,b)-pivot)*dt*1e9
    fig,ax=plt.subplots(figsize=(10,4))
    ax.plot(ns,clock[a:b],color='#a87300',label=f'{clock_line} clock',linewidth=1)
    ax.plot(ns,data[a:b],color='#007c99',label=f'{data_line} data',linewidth=1)
    ax.axhline(threshold,color='gray',linestyle='--',linewidth=.6)
    ax.set(xlabel='Time from clock threshold crossing (ns)',ylabel='V',ylim=(-1.3,4.6),
           title='Measured data setup and edge transients (1 ns samples)')
    ax.legend(); ax.grid(alpha=.25); fig.tight_layout()
    fig.savefig(output/f'measured-frame{frame}-edge.png',dpi=150); plt.close(fig)
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
