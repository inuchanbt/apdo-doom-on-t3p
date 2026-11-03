"""Bounded MHO984 control for the CM-only waveform experiment.

Uses the user-provided instrument address and verifies RIGOL MHO984 first.
No factory reset, output generator, firmware update or CM transmit command.
The prepare action preserves the old acquisition separately before use.
"""
import argparse
import array
import json
from pathlib import Path
import socket
import sys
import time


class Scope:
    def __init__(self,host,port=5555):
        self.socket=socket.create_connection((host,port),timeout=4)
        self.socket.settimeout(20)
        self.file=self.socket.makefile('rb')
        fields=self.query('*IDN?').split(',')
        if len(fields)<2 or 'RIGOL' not in fields[0].upper() or fields[1].strip()!='MHO984':
            self.close(); raise ValueError('Instrument is not expected RIGOL MHO984')

    def close(self):
        self.file.close(); self.socket.close()

    def write(self,command):
        if '\n' in command or '\r' in command: raise ValueError('One command only')
        self.socket.sendall(command.encode('ascii')+b'\n')

    def query_bytes(self,command,limit=16000000):
        self.write(command)
        first=self.file.read(1)
        if first==b'#':
            digits=self.file.read(1)
            if digits not in b'123456789' or len(digits)!=1: raise ValueError('Invalid binary header')
            length=int(self.file.read(int(digits)))
            if not 0<length<=limit: raise ValueError('Response outside size limit')
            value=self.file.read(length)
            if len(value)!=length: raise IOError('Truncated response')
            end=self.file.read(1)
            if end==b'\r': end=self.file.read(1)
            if end!=b'\n': raise ValueError('Missing response terminator')
            return value
        value=first+self.file.readline(limit)
        if not value.endswith(b'\n'): raise ValueError('Unterminated response')
        return value.rstrip(b'\r\n')

    def query(self,command):
        return self.query_bytes(command,4096).decode('ascii').strip()

    def errors(self):
        result=[]
        for _ in range(16):
            answer=self.query(':SYSTem:ERRor?')
            if answer.startswith('0,') or answer.startswith('+0,'): return result
            result.append(answer)
        raise ValueError('Instrument error queue did not drain')

    def snapshot(self):
        commands=[':ACQuire:SRATe?',':ACQuire:MDEPth?',':TIMebase:MAIN:SCALe?',
                  ':TIMebase:MAIN:OFFSet?',':TRIGger:STATus?',':TRIGger:MODE?',
                  ':TRIGger:SWEep?',':TRIGger:EDGE:LEVel?',':TRIGger:EDGE:SOURce?',
                  ':TRIGger:EDGE:SLOPe?']
        for channel in (1,2,3):
            commands += [f':CHANnel{channel}:{setting}?' for setting in
                         ('DISPlay','SCALe','OFFSet','PROBe','COUPling','IMPedance')]
        return {command:self.query(command) for command in commands}

    def prepare(self,trigger_channel=1,timebase=0.0001):
        if trigger_channel not in (1,2): raise ValueError('Use CH1 or CH2 trigger')
        if timebase not in (0.0001,0.0005,0.001,0.002): raise ValueError('Unexpected timebase')
        before=self.snapshot(); prior_errors=self.errors()
        commands=[':STOP',':ACQuire:TYPE NORMal',':ACQuire:MDEPth 1M',
                  ':TIMebase:MODE MAIN',f':TIMebase:MAIN:SCALe {timebase}',':TIMebase:MAIN:OFFSet 0']
        for channel,offset in ((1,-0.5),(2,-3.5)):
            commands += [f':CHANnel{channel}:{setting}' for setting in
                         ('DISPlay ON','PROBe 10','COUPling DC','IMPedance OMEG','SCALe 1',f'OFFSet {offset}')]
        commands += [':TRIGger:MODE EDGE',f':TRIGger:EDGE:SOURce CHANnel{trigger_channel}',
                     ':TRIGger:EDGE:SLOPe POSitive',':TRIGger:EDGE:LEVel 1.5',':TRIGger:SWEep NORMal']
        for command in commands: self.write(command)
        if self.query('*OPC?')!='1': raise ValueError('Instrument did not complete preparation')
        errors=self.errors()
        if errors: raise ValueError('Scope rejected preparation: '+str(errors))
        after=self.snapshot()
        if after[':TRIGger:EDGE:SOURce?']!=f'CHAN{trigger_channel}':
            raise ValueError('Trigger source mismatch')
        for c in (1,2):
            if float(after[f':CHANnel{c}:SCALe?'])!=1 or after[f':CHANnel{c}:IMPedance?']!='OMEG':
                raise ValueError('Input configuration mismatch')
        if float(after[':ACQuire:MDEPth?'])<1000000 or abs(float(after[':TIMebase:MAIN:SCALe?'])-timebase)>1e-10:
            raise ValueError('Acquisition configuration mismatch')
        return dict(before=before,prior_errors=prior_errors,after=after)

    def arm(self):
        self.write(':SINGle')
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            status=self.query(':TRIGger:STATus?')
            if status in ('WAIT','TD','RUN'): return dict(status=status)
            time.sleep(0.05)
        raise TimeoutError('Scope did not arm')

    def prepare_power_edge(self,timebase=0.0005):
        """CH3 observes the verified divider midpoint, not raw 5V.

        Preserve the existing CH1=D-/CH2=D+ configuration and trigger early
        on the midpoint's 1V rise, so power, key and release fit one record.
        """
        base=self.prepare(2,timebase)
        for command in (':CHANnel3:DISPlay ON',':CHANnel3:PROBe 10',
                        ':CHANnel3:COUPling DC',':CHANnel3:IMPedance OMEG',
                        ':CHANnel3:SCALe 1',':CHANnel3:OFFSet -1.5',
                        ':TRIGger:MODE EDGE',':TRIGger:EDGE:SOURce CHANnel3',
                        ':TRIGger:EDGE:SLOPe POSitive',':TRIGger:EDGE:LEVel 1',
                        ':TRIGger:SWEep NORMal'):
            self.write(command)
        if self.query('*OPC?')!='1': raise ValueError('Power trigger preparation did not complete')
        errors=self.errors()
        if errors: raise ValueError('Power trigger rejected: '+str(errors))
        after=self.snapshot()
        expected={':CHANnel3:IMPedance?':'OMEG',':CHANnel3:COUPling?':'DC',
                  ':TRIGger:MODE?':'EDGE',':TRIGger:EDGE:SOURce?':'CHAN3',
                  ':TRIGger:EDGE:SLOPe?':'POS'}
        if any(after[k]!=v for k,v in expected.items()):
            raise ValueError('Power trigger readback mismatch')
        for k,v in ((':CHANnel3:SCALe?',1),(':CHANnel3:PROBe?',10),
                    (':CHANnel3:OFFSet?',-1.5),(':TRIGger:EDGE:LEVel?',1)):
            if abs(float(after[k])-v)>1e-6: raise ValueError('Power channel parameter mismatch')
        return dict(base=base,after=after,power_signal='CH3 divider midpoint, VBUS approximately twice this voltage')

    def prepare_key_pulse(self,trigger_channel=2,timebase=0.0005):
        base=self.prepare(trigger_channel,timebase)
        # Positive key clock is ~9.667us. A bounded width excludes brief
        # contact noise, long idle-high periods and normal full-speed USB bits.
        for command in (':TRIGger:MODE PULSe',
                        f':TRIGger:PULSe:SOURce CHANnel{trigger_channel}',
                        ':TRIGger:PULSe:POLarity POSitive',
                        ':TRIGger:PULSe:WHEN GLESs',
                        ':TRIGger:PULSe:UWIDth 0.000011',
                        ':TRIGger:PULSe:LWIDth 0.000008',
                        ':TRIGger:PULSe:LEVel 1.5',':TRIGger:SWEep NORMal'):
            self.write(command)
        if self.query('*OPC?')!='1': raise ValueError('Pulse preparation did not complete')
        errors=self.errors()
        if errors: raise ValueError('Pulse trigger rejected: '+str(errors))
        readback={k:self.query(k) for k in (':TRIGger:MODE?',':TRIGger:PULSe:SOURce?',
                  ':TRIGger:PULSe:POLarity?',':TRIGger:PULSe:WHEN?',
                  ':TRIGger:PULSe:UWIDth?',':TRIGger:PULSe:LWIDth?',':TRIGger:PULSe:LEVel?')}
        expected={':TRIGger:MODE?':'PULS',':TRIGger:PULSe:SOURce?':f'CHAN{trigger_channel}',
                  ':TRIGger:PULSe:POLarity?':'POS',':TRIGger:PULSe:WHEN?':'GLES'}
        if any(readback[k]!=v for k,v in expected.items()): raise ValueError('Pulse mode readback mismatch: '+str(readback))
        for k,v in ((':TRIGger:PULSe:UWIDth?',11e-6),(':TRIGger:PULSe:LWIDth?',8e-6),(':TRIGger:PULSe:LEVel?',1.5)):
            if abs(float(readback[k])-v)>max(1e-10,abs(v)*1e-5): raise ValueError('Pulse parameter mismatch')
        return dict(base=base,pulse_readback=readback)

    def wait_stopped(self,seconds):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            if self.query(':TRIGger:STATus?')=='STOP': return True
            time.sleep(0.1)
        return False

    def capture(self,folder,channels=(1,2)):
        channels=tuple(channels)
        if channels not in ((1,2),(1,2,3)): raise ValueError('Capture CH1/2 or CH1/2/3 in order')
        if self.query(':TRIGger:STATus?')!='STOP': raise ValueError('Scope must be stopped for RAW capture')
        folder.mkdir(parents=True,exist_ok=True)
        points=int(float(self.query(':ACQuire:MDEPth?')))
        if not 1<=points<=2000000: raise ValueError('Unexpected capture length')
        results=[]
        for channel in channels:
            for command in [f':WAVeform:SOURce CHANnel{channel}',':WAVeform:MODE RAW',
                            ':WAVeform:FORMat WORD',f':WAVeform:POINts {points}',
                            ':WAVeform:STARt 1',f':WAVeform:STOP {points}']:
                self.write(command)
            if self.query('*OPC?')!='1': raise ValueError('Waveform setup failed')
            preamble=self.query(':WAVeform:PREamble?')
            pre=[float(x) for x in preamble.split(',')]
            raw=self.query_bytes(':WAVeform:DATA?')
            if len(raw)!=points*2: raise ValueError('Waveform word count differs from memory depth')
            (folder/f'ch{channel}.u16le').write_bytes(raw)
            values=array.array('H'); values.frombytes(raw)
            if sys.byteorder!='little': values.byteswap()
            # Validate byte order and scale against the scope's own ASCII voltages.
            self.write(':WAVeform:FORMat ASCii'); self.write(':WAVeform:STARt 1'); self.write(':WAVeform:STOP 32')
            ascii_values=[float(x) for x in self.query_bytes(':WAVeform:DATA?').decode().split(',') if x.strip()]
            expected=[(x-pre[8]-pre[9])*pre[7] for x in values[:32]]
            if len(ascii_values)!=32 or max(abs(a-b) for a,b in zip(ascii_values,expected))>max(0.001,pre[7]*2):
                raise ValueError('WORD conversion does not match instrument ASCII voltage')
            info=dict(channel=channel,preamble=preamble,points=points,word_byte_order='little',
                      conversion_checked_against_ascii=True,xincrement=pre[4],xorigin=pre[5],xreference=pre[6],
                      yincrement=pre[7],yorigin=pre[8],yreference=pre[9],
                      min_v=(min(values)-pre[8]-pre[9])*pre[7],max_v=(max(values)-pre[8]-pre[9])*pre[7],
                      adc_min_clipped=sum(x==0 for x in values),adc_max_clipped=sum(x==65535 for x in values))
            results.append(info)
        (folder/'metadata.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
        return results


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['snapshot','prepare','prepare-key-pulse','prepare-power-edge','arm','status','capture'])
    parser.add_argument('--host',required=True)
    parser.add_argument('--port',type=int,default=5555)
    parser.add_argument('--trigger-channel',type=int,choices=[1,2],default=1)
    parser.add_argument('--timebase',type=float,choices=[0.0001,0.0005,0.001,0.002],default=0.0001)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--channels',type=int,nargs='+',choices=[1,2,3],default=[1,2])
    args=parser.parse_args(); scope=Scope(args.host,args.port)
    try:
        if args.action=='snapshot': result=scope.snapshot()
        elif args.action=='prepare': result=scope.prepare(args.trigger_channel,args.timebase)
        elif args.action=='prepare-key-pulse': result=scope.prepare_key_pulse(args.trigger_channel,args.timebase)
        elif args.action=='prepare-power-edge': result=scope.prepare_power_edge(args.timebase)
        elif args.action=='arm': result=scope.arm()
        elif args.action=='status': result=dict(status=scope.query(':TRIGger:STATus?'))
        else: result=scope.capture(args.output,args.channels)
        if args.action!='capture':
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(result,indent=2))
    finally: scope.close()


if __name__=='__main__': main()
