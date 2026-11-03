"""Verify no command reaches a mismatched profile and check weak-pull failures."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('cm_commands',ROOT/'scripts/cm_input_commands.py')
cm=importlib.util.module_from_spec(spec)
spec.loader.exec_module(cm)


def sample(phase='disabled',bits=(0,0),**updates):
    item=dict(program='t3p_cm_input_check',version=2,mode='inputs_only',
              board='RP2350-USB-CM',dp_gpio=13,dm_gpio=12,
              dp=bits[0],dm=bits[1],pulls=phase,uptime_ms=100)
    item.update(updates)
    return json.dumps(item).encode()+b'\n'


class Port:
    def __init__(self,initial=None,stuck=False,short=False):
        self.lines=[sample() if initial is None else initial]
        self.writes=[]
        self.stuck=stuck
        self.short=short

    def readline(self,size):
        return self.lines.pop(0) if self.lines else b''

    def write(self,data):
        self.writes.append(data)
        if data==b'PULLCHECK\n':
            self.lines += [sample(phase,(1,1) if self.stuck else bits or (0,0))
                           for phase,bits in cm.PHASES]
        return len(data)-int(self.short)


class Commands(unittest.TestCase):
    def test_three_complete_cycles_and_release(self):
        port=Port()
        result=cm.run_command(port,'pull-check',3)
        self.assertTrue(result['success'])
        self.assertEqual(port.writes,[b'PULLCHECK\n']*3)
        self.assertTrue(all(c['readings'][-1]['pulls']=='disabled' for c in result['cycles']))

    def test_wrong_board_or_pins_never_written(self):
        for updates in [dict(board='T3P'),dict(dp_gpio=12),dict(dm_gpio=13),dict(version=99)]:
            port=Port(initial=sample(**updates))
            with self.assertRaises(ValueError): cm.run_command(port,'bootsel')
            self.assertEqual(port.writes,[])

    def test_v1_cannot_receive_pullcheck(self):
        port=Port(initial=sample(version=1))
        with self.assertRaises(ValueError): cm.run_command(port,'pull-check')
        self.assertEqual(port.writes,[])

    def test_stuck_lines_stop_after_one_cycle(self):
        port=Port(stuck=True)
        result=cm.run_command(port,'pull-check',3)
        self.assertFalse(result['success'])
        self.assertEqual(len(result['cycles']),1)
        self.assertEqual(result['cycles'][0]['readings'][-1]['pulls'],'disabled')
        self.assertEqual(port.writes,[b'PULLCHECK\n'])

    def test_short_write_stops(self):
        port=Port(short=True)
        with self.assertRaises(IOError): cm.run_command(port,'pull-check')
        self.assertEqual(len(port.writes),1)

    def test_bootsel_is_the_only_other_command(self):
        port=Port(initial=sample(version=1))
        result=cm.run_command(port,'bootsel')
        self.assertEqual(result['command'],'BOOTSEL')
        self.assertEqual(port.writes,[b'BOOTSEL\n'])
        port=Port()
        with self.assertRaises(ValueError): cm.run_command(port,'anything-else')
        self.assertEqual(port.writes,[])


if __name__=='__main__': unittest.main()
