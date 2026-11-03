"""CM command preconditions and failure behavior, with no USB device opened."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import cm_power_probe as probe

def profile(**changes):
    fields=dict(program='t3p_cm_power_probe',version=3,board='RP2350-USB-CM',
        event='status',key_clock_gpio=13,key_data_gpio=12,clock_hz=120000000,
        armed=False,used=False,host_requested=False,fault=False,frames=0,
        bias_gpio=10,bias_active=False,bias_checked=True,adc_gpio=26,adc_channel=0,
        adc_raw=0,adc_on_checked=True,adc_off_checked=True,key_delay_us=1000,
        power_policy='stable_before_first_key',power_settle_limit_us=50000)
    fields.update(changes); return fields

class Port:
    def __init__(self,*items,short=False): self.items=list(items); self.writes=[]; self.short=short
    def readline(self,size): return json.dumps(self.items.pop(0)).encode()+b'\n'
    def write(self,data): self.writes.append(data); return len(data)-int(self.short)

class Gates(unittest.TestCase):
    def run_port(self,port,action=None):
        root=Path(__file__).resolve().parents[1]/'local/test-tmp'
        root.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as folder, contextlib.redirect_stdout(io.StringIO()):
            return probe.operate(port,Path(folder)/'record.json',action,2)

    def test_foreign_mapping_profile_never_receives_command(self):
        for wrong in (dict(program='t3p_cm_rom_probe'),dict(adc_gpio=27),dict(adc_channel=1),
                      dict(key_clock_gpio=12),dict(key_delay_us=0),dict(version=4),dict(adc_raw=True)):
            port=Port(profile(**wrong))
            with self.assertRaises(ValueError): self.run_port(port,'arm')
            self.assertEqual(port.writes,[])

    def test_stable_policy_required_and_v1_only_recovery_not_power_arm(self):
        for wrong in (dict(power_policy='first_edge'),dict(power_settle_limit_us=50001),dict(version=1,key_delay_us=100)):
            port=Port(profile(**wrong))
            with self.assertRaises(ValueError): self.run_port(port,'power-bench')
            self.assertEqual(port.writes,[])
        port=Port(profile(version=1,key_delay_us=100,fault=True,used=True))
        self.assertTrue(self.run_port(port,'bootsel')['success'])
        self.assertEqual(port.writes,[b'BOOTSEL\n'])

    def test_arm_requires_off_voltage_and_all_preflights(self):
        for wrong in (dict(adc_raw=311),dict(adc_on_checked=False),dict(adc_off_checked=False),
                      dict(bias_checked=False),dict(bias_active=True),dict(used=True),
                      dict(armed=True),dict(fault=True),dict(host_requested=True),dict(frames=1)):
            port=Port(profile(**wrong))
            with self.assertRaises(ValueError): self.run_port(port,'arm')
            self.assertEqual(port.writes,[])

    def test_on_actions_require_voltage_and_off_check_order(self):
        for action in ('adc-on','bias','bench','host'):
            for raw in (0,2482,3353,4095):
                port=Port(profile(adc_raw=raw))
                with self.assertRaises(ValueError): self.run_port(port,action)
                self.assertEqual(port.writes,[])
        port=Port(profile(adc_on_checked=False))
        with self.assertRaises(ValueError): self.run_port(port,'adc-off')
        self.assertEqual(port.writes,[])

    def test_adc_checks_only_send_the_requested_adc_command(self):
        for on in (False,True):
            port=Port(profile(adc_raw=3150 if on else 0),
                dict(event='adc_check_finished',on=on,passed=True,
                     adc_on_checked=True,adc_off_checked=not on))
            self.assertTrue(self.run_port(port,'adc-on' if on else 'adc-off')['success'])
            self.assertEqual(port.writes,[b'ADC_ON_CHECK\n' if on else b'ADC_OFF_CHECK\n'])

    def test_voltage_and_output_faults_never_retry(self):
        for failure in sorted(probe.FAULTS):
            port=Port(profile(),dict(event='armed'),dict(event=failure))
            result=self.run_port(port,'arm')
            self.assertTrue(result['controller_fault']); self.assertFalse(result['success'])
            self.assertEqual(port.writes,[b'ARM_STABLE_POWER_ONCE\n'])

    def test_arm_cancel_requires_unconsumed_no_output_status(self):
        port=Port(profile(),dict(event='armed'),profile())
        self.assertTrue(self.run_port(port,'check-arm')['success'])
        self.assertEqual(port.writes,[b'ARM_STABLE_POWER_ONCE\n',b'DISARM\n'])
        port=Port(profile(),dict(event='armed'),profile(used=True))
        self.assertFalse(self.run_port(port,'check-arm')['success'])

    def test_short_write_and_rejection_never_retry(self):
        for port in (Port(profile(),short=True),Port(profile(),dict(event='arm_rejected'))):
            with self.assertRaises((IOError,ValueError)): self.run_port(port,'arm')
            self.assertEqual(port.writes,[b'ARM_STABLE_POWER_ONCE\n'])

    def test_host_mode_never_sends_key_and_only_mount_counts_rom_identity(self):
        port=Port(profile(adc_raw=3150),dict(event='device_mounted',rom_identity_match=False))
        result=self.run_port(port,'host')
        self.assertTrue(result['success']); self.assertFalse(result['rom_identity_seen'])
        self.assertEqual(port.writes,[b'HOST_START\n'])

    def test_observer_of_running_host_sends_no_command(self):
        port=Port(profile(adc_raw=3100,used=True,frames=1,host_requested=True),
                  dict(event='device_mounted',stock_identity_match=True,rom_identity_match=False))
        result=self.run_port(port)
        self.assertTrue(result['success']); self.assertTrue(result['stock_identity_seen'])
        self.assertFalse(result['rom_identity_seen']); self.assertEqual(port.writes,[])

    def test_bench_requires_release_and_explicit_no_low_bias_result(self):
        for low in (False,True):
            port=Port(profile(adc_raw=3150),
                dict(event='key_finished',completed=True,underrun=False,
                     pio_oe_before_cpu_release=0,fault=False,frames=1),
                dict(event='biased_line_observation',bias_released=True,low_pulse_candidate=low),
                dict(event='host_started'))
            self.assertEqual(self.run_port(port,'bench')['success'],not low)
            self.assertEqual(port.writes,[b'BENCH_FRAME_AND_HOST\n'])

    def test_power_bench_stops_at_host_only_after_one_recorded_power_edge(self):
        tail=[dict(event='key_start_timing',edge_detected=True,actual_enable_delay_us=1112),
              dict(event='key_finished',completed=True,underrun=False,
                   pio_oe_before_cpu_release=0,fault=False,frames=1),
              dict(event='biased_line_observation',bias_released=True,low_pulse_candidate=False),
              dict(event='host_started')]
        for edges in ([],[dict(event='power_edge')],[dict(event='power_edge')]*2):
            port=Port(profile(),dict(event='armed'),*edges,*tail)
            self.assertEqual(self.run_port(port,'power-bench')['success'],len(edges)==1)
            self.assertEqual(port.writes,[b'ARM_STABLE_POWER_ONCE\n'])
        for wrong in (dict(adc_raw=311),dict(adc_off_checked=False),dict(bias_checked=False)):
            port=Port(profile(**wrong))
            with self.assertRaises(ValueError): self.run_port(port,'power-bench')
            self.assertEqual(port.writes,[])

    def test_power_bench_accepts_settling_without_retransmitting(self):
        port=Port(profile(),dict(event='armed'),dict(event='power_candidate'),
            dict(event='power_bounce'),dict(event='power_candidate'),
            dict(event='power_edge',bounces_before_key=1),
            dict(event='key_start_timing',edge_detected=True,actual_enable_delay_us=1128),
            dict(event='key_finished',completed=True,underrun=False,
                 pio_oe_before_cpu_release=0,fault=False,frames=1),
            dict(event='biased_line_observation',bias_released=True,low_pulse_candidate=False),
            dict(event='host_started'))
        self.assertTrue(self.run_port(port,'power-bench')['success'])
        self.assertEqual(port.writes,[b'ARM_STABLE_POWER_ONCE\n'])

    def test_delay_selection_requires_v3_and_never_emits_key(self):
        for delay in (100,1000,5000):
            port=Port(profile(),dict(event='delay_set',key_delay_us=delay,signal_sent=False))
            self.assertTrue(self.run_port(port,'delay-'+str(delay))['success'])
            self.assertEqual(port.writes,[('DELAY_'+str(delay)+'\n').encode()])
        for changes in (dict(version=2,key_delay_us=100),dict(used=True),dict(armed=True),
                        dict(fault=True),dict(host_requested=True),dict(bias_active=True)):
            port=Port(profile(**changes))
            with self.assertRaises(ValueError): self.run_port(port,'delay-1000')
            self.assertEqual(port.writes,[])
        for event in (dict(event='delay_set',key_delay_us=100,signal_sent=False),
                      dict(event='delay_set',key_delay_us=1000,signal_sent=True)):
            self.assertFalse(self.run_port(Port(profile(),event),'delay-1000')['success'])

    def test_legacy_v2_profile_is_still_valid_and_delay_bounds_are_exact(self):
        self.assertEqual(probe.identify(profile(version=2,key_delay_us=100))['version'],2)
        for changes in (dict(version=2,key_delay_us=1000),dict(key_delay_us=True),dict(key_delay_us=999)):
            with self.assertRaises(ValueError): probe.identify(profile(**changes))

    def test_keep_waiting_renews_before_power_then_never_rearms_after_key(self):
        clock=[0.0]
        class TimedPort(Port):
            def readline(self,size):
                delta,item=self.items.pop(0);clock[0]+=delta
                return json.dumps(item).encode()+b'\n'
        port=TimedPort((0,profile()),(0,dict(event='armed')),(.02,profile(armed=True)),
            (.001,profile()),(.001,dict(event='armed')),(.001,dict(event='power_candidate')),
            (.1,dict(event='key_finished',completed=True,underrun=False,
                       pio_oe_before_cpu_release=0,fault=False,frames=1)),
            (.001,dict(event='device_mounted',stock_identity_match=True,rom_identity_match=False)))
        lease_type=probe.WaitLease
        root=Path(__file__).resolve().parents[1]/'local/test-tmp';root.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as folder,contextlib.redirect_stdout(io.StringIO()), \
             mock.patch.object(probe.time,'monotonic',side_effect=lambda:clock[0]), \
             mock.patch.object(probe,'WaitLease',side_effect=lambda:lease_type(interval=.01)):
            result=probe.operate(port,Path(folder)/'record.json','arm',20,True)
        self.assertTrue(result['success']);self.assertEqual(result['wait_renewals'],1)
        self.assertEqual(port.writes,[b'ARM_STABLE_POWER_ONCE\n',b'DISARM\n',b'ARM_STABLE_POWER_ONCE\n'])

if __name__=='__main__': unittest.main()
