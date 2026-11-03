"""Scope-only command gates: never emit on biased pins or wrong firmware."""
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import cm_wave_scope as wave


class Port:
    def __init__(self,finished=None,short=False,**changes):
        profile=dict(program='t3p_cm_wave_scope',version=1,board='RP2350-USB-CM',
                     event='status',dp_gpio=13,dm_gpio=12,bench_only=True,pulls='down',
                     pio_oe=0,clock_hz=120000000,fault=False,dp=0,dm=0,frames=0)
        profile.update(changes)
        self.lines=[profile,finished or dict(event='wave_finished',completed=True,
                                           underrun=False,pio_oe=0,frames=1)]
        self.writes=[]; self.short=short
    def readline(self,size): return json.dumps(self.lines.pop(0)).encode()+b'\n'
    def write(self,data): self.writes.append(data); return len(data)-int(self.short)


class Gates(unittest.TestCase):
    def test_wrong_or_biased_state_never_emits(self):
        for changes in [dict(program='t3p_cm_host_probe'),dict(bench_only=False),
                        dict(clock_hz=150000000),dict(pio_oe=1),dict(fault=True),
                        dict(dp=1),dict(dm=1)]:
            port=Port(**changes)
            with self.assertRaises(ValueError): wave.operate(port,emit=True)
            self.assertEqual(port.writes,[])

    def test_one_frame_only(self):
        port=Port(); result=wave.operate(port,emit=True)
        self.assertTrue(result['success'])
        self.assertEqual(port.writes,[b'SCOPE_KEY_ONCE\n'])

    def test_underrun_or_enabled_output_fails_without_retry(self):
        for fields in [dict(underrun=True),dict(pio_oe=1),dict(completed=False),dict(frames=2)]:
            finished=dict(event='wave_finished',completed=True,underrun=False,pio_oe=0,frames=1)
            finished.update(fields)
            port=Port(finished=finished)
            self.assertFalse(wave.operate(port,emit=True)['success'])
            self.assertEqual(len(port.writes),1)

    def test_short_write_never_retried(self):
        port=Port(short=True)
        with self.assertRaises(IOError): wave.operate(port,emit=True)
        self.assertEqual(len(port.writes),1)

    def test_v2_requires_hardware_release_before_fallback(self):
        for value in (None,1,3):
            result=dict(event='wave_finished',completed=True,underrun=False,pio_oe=0,frames=1)
            if value is not None: result['pio_oe_before_cpu_release']=value
            port=Port(version=2,finished=result)
            self.assertFalse(wave.operate(port,emit=True)['success'])
            self.assertEqual(len(port.writes),1)

    def test_identified_faulted_cm_can_still_reboot_itself(self):
        port=Port(fault=True,pio_oe=3)
        self.assertTrue(wave.operate(port,bootsel=True)['success'])
        self.assertEqual(port.writes,[b'BOOTSEL\n'])

    def test_v3_pad_configuration_must_be_verified_before_emit(self):
        port=Port(version=3,drive_2ma=False,slew_slow=True)
        with self.assertRaises(ValueError): wave.operate(port,emit=True)
        self.assertEqual(port.writes,[])

    def test_v4_requires_br35_pin_mapping_and_hardware_release(self):
        finished=dict(event='wave_finished',completed=True,underrun=False,
                      pio_oe=0,pio_oe_before_cpu_release=0,frames=1)
        profile=dict(version=4,drive_2ma=True,slew_slow=True,
                     key_clock_gpio=13,key_data_gpio=12)
        self.assertTrue(wave.operate(Port(finished=finished,**profile),emit=True)['success'])
        for changes in (dict(key_clock_gpio=12),dict(key_data_gpio=13),dict(drive_2ma=False)):
            bad=dict(profile); bad.update(changes)
            port=Port(finished=finished,**bad)
            with self.assertRaises(ValueError): wave.operate(port,emit=True)
            self.assertEqual(port.writes,[])


if __name__=='__main__': unittest.main()
