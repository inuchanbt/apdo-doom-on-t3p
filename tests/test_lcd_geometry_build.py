"""Check the fixed helper layout and reject ELF sections spilling into stock code."""
import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_lcd_probe import ROOT, elf_sections

class GeometryELF(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data=(ROOT/'artifacts/lcd-geometry/lcd_geometry.elf').read_bytes()

    def test_fixed_sections_fit_original_slots(self):
        self.assertEqual({n:len(b) for n,b in elf_sections(self.data).items()},
                         {'.probe_draw':142,'.probe_continue':166})

    def test_reject_spill_and_writable_code(self):
        shoff=struct.unpack_from('<I',self.data,32)[0]
        for section,field,value in ((1,20,145),(2,20,209),(1,8,7),(2,12,0xc01106c)):
            data=bytearray(self.data)
            struct.pack_into('<I',data,shoff+section*40+field,value)
            with self.assertRaises(ValueError): elf_sections(data)

if __name__=='__main__':unittest.main()
