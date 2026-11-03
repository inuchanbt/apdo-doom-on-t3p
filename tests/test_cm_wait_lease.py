"""No hardware opened: wait extension cannot become a signal retry."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from cm_wait_lease import WaitLease,ARM,DISARM

def status(**kw):
    s=dict(event='status',armed=True,used=False,host_requested=False,fault=False,
           bias_active=False,frames=0,adc_raw=0,adc_on_checked=True,
           adc_off_checked=True,bias_checked=True)
    s.update(kw); return s

class Renewal(unittest.TestCase):
    def active(self):
        g=WaitLease();self.assertIsNone(g.feed(dict(event='armed'),0));return g

    def test_requires_explicit_disarm_ack_and_then_new_arm_ack(self):
        g=self.active()
        self.assertIsNone(g.feed(status(),239.99))
        self.assertEqual(g.feed(status(),240),DISARM)
        self.assertIsNone(g.feed(status(),240.1))
        self.assertEqual(g.feed(status(armed=False),240.2),ARM)
        self.assertIsNone(g.feed(dict(event='armed'),240.3))
        self.assertEqual(g.renewals,1)
        self.assertIsNone(g.feed(status(),480.29))
        self.assertEqual(g.feed(status(),480.3),DISARM)

    def test_power_activity_permanently_disables_renewal(self):
        for event in ('power_candidate','power_bounce','power_edge','key_finished',
                      'host_started','device_mounted','arm_expired'):
            g=self.active();g.feed(dict(event=event),239)
            self.assertIsNone(g.feed(status(),240))
            self.assertIsNone(g.feed(dict(event='armed'),241))
            self.assertIsNone(g.feed(status(),1000))

    def test_used_fault_host_output_or_voltage_never_renews(self):
        for change in (dict(used=True),dict(fault=True),dict(host_requested=True),
                       dict(frames=1),dict(bias_active=True),dict(adc_raw=311)):
            g=self.active();self.assertIsNone(g.feed(status(**change),240))
            self.assertIsNone(g.feed(status(),500))

    def test_power_race_or_lost_checks_after_disarm_sends_no_new_arm(self):
        for change in (dict(adc_raw=311),dict(bias_checked=False),dict(adc_off_checked=False)):
            g=self.active();self.assertEqual(g.feed(status(),240),DISARM)
            with self.assertRaises(ValueError):g.feed(status(armed=False,**change),240.1)
            self.assertIsNone(g.feed(status(armed=False),241))

    def test_key_completed_during_renewal_never_gets_another_arm(self):
        g=self.active();self.assertEqual(g.feed(status(),240),DISARM)
        self.assertIsNone(g.feed(status(armed=False,used=True,frames=1,host_requested=True),240.1))
        self.assertIsNone(g.feed(status(armed=False),241))

    def test_late_ack_and_unexpected_end_fail_closed(self):
        g=self.active();self.assertEqual(g.feed(status(),240),DISARM)
        self.assertEqual(g.read_deadline(3600),243)
        with self.assertRaises(TimeoutError):g.feed(status(armed=False),243)
        self.assertIsNone(g.feed(status(armed=False),244))
        g=self.active()
        with self.assertRaises(ValueError):g.feed(status(armed=False),20)

if __name__=='__main__':unittest.main()
