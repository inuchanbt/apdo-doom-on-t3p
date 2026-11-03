"""Command gates for the one-attach probe, with no serial devices opened."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import cm_rom_probe as probe


def profile(**kw):
    result=dict(program='t3p_cm_rom_probe',version=1,board='RP2350-USB-CM',event='status',
                key_clock_gpio=13,key_data_gpio=12,clock_hz=120000000,armed=False,
                used=False,host_requested=False,fault=False,frames=0)
    result.update(kw); return result


class Port:
    def __init__(self,*items,short=False): self.items=list(items); self.writes=[]; self.short=short
    def readline(self,size): return json.dumps(self.items.pop(0)).encode()+b'\n'
    def write(self,data): self.writes.append(data); return len(data)-int(self.short)


class Gates(unittest.TestCase):
    def run_port(self,port,**kw):
        root=Path(__file__).resolve().parents[1]/'local/test-tmp'
        root.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as folder, contextlib.redirect_stdout(io.StringIO()):
            return probe.operate(port,Path(folder)/'record.json',seconds=2,**kw)

    def test_wrong_firmware_or_mapping_never_commanded(self):
        for changes in (dict(program='t3p_cm_wave_scope'),dict(key_clock_gpio=12),dict(clock_hz=150000000),dict(version=4)):
            port=Port(profile(**changes))
            with self.assertRaises(ValueError): self.run_port(port,arm=True)
            self.assertEqual(port.writes,[])

    def test_v2_extended_wait_profile_is_recognized(self):
        self.assertEqual(probe.identify(profile(version=2))['version'],2)

    def test_v3_bias_preflight_required_before_arm(self):
        for changes in (dict(bias_gpio=9),dict(bias_checked=False),dict(bias_active=True)):
            fields=dict(version=3,bias_gpio=10,bias_checked=True,bias_active=False); fields.update(changes)
            port=Port(profile(**fields))
            with self.assertRaises(ValueError): self.run_port(port,arm=True)
            self.assertEqual(port.writes,[])

    def test_v3_bias_check_never_sends_key(self):
        port=Port(profile(version=3,bias_gpio=10,bias_checked=False,bias_active=False),
                  dict(event='bias_check_finished',passed=True,high_seen=True,low_seen=True,bias_released=True))
        self.assertTrue(self.run_port(port,bias_check=True)['bias_check_pass'])
        self.assertEqual(port.writes,[b'BIAS_CHECK\n'])

    def test_held_low_or_release_fault_stops_without_retry(self):
        for failure in ('line_held_low_timeout','bias_release_failed'):
            port=Port(profile(version=3,bias_gpio=10,bias_checked=True,bias_active=False),
                      dict(event='armed'),dict(event=failure))
            self.assertTrue(self.run_port(port,arm=True)['controller_fault'])
            self.assertEqual(port.writes,[b'ARM_ATTACH_ONCE\n'])

    def test_used_or_active_state_never_rearmed(self):
        for changes in (dict(armed=True),dict(used=True),dict(host_requested=True),dict(fault=True),dict(frames=1)):
            port=Port(profile(**changes))
            with self.assertRaises(ValueError): self.run_port(port,arm=True)
            self.assertEqual(port.writes,[])

    def test_arm_disarm_sends_no_wave_command(self):
        port=Port(profile(),dict(event='armed'),profile())
        self.assertTrue(self.run_port(port,check_arm=True)['check_arm_pass'])
        self.assertEqual(port.writes,[b'ARM_ATTACH_ONCE\n',b'DISARM\n'])

    def test_bench_requires_one_complete_release_then_host(self):
        key=dict(event='key_finished',completed=True,underrun=False,
                 pio_oe_before_cpu_release=0,frames=1,fault=False)
        for oe in (0,1):
            changed=dict(key,pio_oe_before_cpu_release=oe)
            port=Port(profile(),changed,dict(event='host_started'))
            self.assertEqual(self.run_port(port,bench=True)['bench_pass'],oe==0)
            self.assertEqual(port.writes,[b'BENCH_FRAME_AND_HOST\n'])

    def test_short_command_write_never_retried(self):
        port=Port(profile(),short=True)
        with self.assertRaises(IOError): self.run_port(port,arm=True)
        self.assertEqual(port.writes,[b'ARM_ATTACH_ONCE\n'])

    def test_only_mounted_rom_identity_counts(self):
        port=Port(profile(),dict(event='armed'),dict(event='device_mounted',rom_identity_match=False))
        self.assertFalse(self.run_port(port,arm=True)['rom_identity_seen'])


if __name__=='__main__': unittest.main()
