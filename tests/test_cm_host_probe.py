"""Check profile/action gates before any host-start command is sent."""
import importlib.util
import json
from pathlib import Path
import unittest

r=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('probe',r/'scripts/cm_host_probe.py')
probe=importlib.util.module_from_spec(spec); spec.loader.exec_module(probe)


class Port:
    def __init__(self,**changes):
        self.profile=dict(program='t3p_cm_host_probe',version=1,board='RP2350-USB-CM',
                          event='status',dp_gpio=13,dm_gpio=12,host_requested=False)
        self.profile.update(changes); self.writes=[]
    def readline(self,size): return json.dumps(self.profile).encode()+b'\n'
    def write(self,data): self.writes.append(data); return len(data)


class Gates(unittest.TestCase):
    def test_wrong_profiles_never_receive_bootsel(self):
        for change in [dict(program='t3p_cm_input_check'),dict(board='T3P'),
                       dict(dp_gpio=12),dict(version=99),dict(host_requested=0)]:
            port=Port(**change)
            with self.assertRaises(ValueError): probe.run_probe(port,bootsel=True)
            self.assertEqual(port.writes,[])

    def test_no_second_host_start(self):
        port=Port(host_requested=True)
        with self.assertRaises(ValueError): probe.run_probe(port,start=True)
        self.assertEqual(port.writes,[])

    def test_only_bootsel_command_is_sent(self):
        port=Port()
        result=probe.run_probe(port,bootsel=True)
        self.assertEqual(port.writes,[b'BOOTSEL\n'])
        self.assertEqual(result['command'],'BOOTSEL')

    def test_invalid_requests_never_written(self):
        for args in [dict(start=True,bootsel=True),dict(start=True,seconds=100),
                     dict(query='update'),dict(query='settings'),
                     dict(query='settings',start=True)]:
            port=Port()
            with self.assertRaises(ValueError): probe.run_probe(port,**args)
            self.assertEqual(port.writes,[])


if __name__=='__main__': unittest.main()
