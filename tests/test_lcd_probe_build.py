"""Reject incompatible ELF layouts before any update file can be assembled."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from build_lcd_probe import ROOT, elf_sections

class ProbeELF(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.elf = (ROOT/'artifacts/lcd-probe/lcd_probe.elf').read_bytes()

    def test_accept_only_the_two_fixed_readonly_executable_hooks(self):
        sections = elf_sections(self.elf)
        self.assertEqual({n:len(p) for n,p in sections.items()},
                         {'.probe_draw':144, '.probe_continue':160})

    def test_reject_capacity_overrun_and_changed_address(self):
        shoff = struct.unpack_from('<I', self.elf, 32)[0]
        for field, value in ((20,145), (12,0xc00d63a)):
            broken = bytearray(self.elf)
            struct.pack_into('<I',broken,shoff+40+field,value)
            with self.assertRaises(ValueError): elf_sections(broken)

    def test_reject_writable_or_unexpected_allocated_section(self):
        shoff = struct.unpack_from('<I', self.elf, 32)[0]
        for field, value in ((8,7), (0,0)):
            broken = bytearray(self.elf)
            struct.pack_into('<I',broken,shoff+40+field,value)
            with self.assertRaises(ValueError): elf_sections(broken)

    def test_reject_engine_linkcheck_as_probe_payload(self):
        with self.assertRaises(ValueError):
            elf_sections((ROOT/'artifacts/pi32v2-engine/DO-NOT-FLASH-linkcheck.elf').read_bytes())

if __name__ == '__main__': unittest.main()
