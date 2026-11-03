"""Exercise the hardware-file gate against damaged copies of the built image."""
import importlib.util
import os
from pathlib import Path
import struct
import unittest
from unittest.mock import Mock

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('cm_build',ROOT/'scripts/build_cm_input_check.py')
build=importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class Uf2Gate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original=(ROOT/os.environ.get('T3P_CM_UF2_FILE','artifacts/cm-input-check/RP2350_CM_INPUT_CHECK_ONLY.uf2')).read_bytes()

    def inspect(self,data):
        return build.inspect_uf2(Mock(read_bytes=lambda: data))

    def test_built_image(self):
        result=self.inspect(self.original)
        self.assertEqual(result['family_ids'],['0xe48bff59'])
        self.assertGreater(result['program_blocks'],0)

    def test_wrong_chip_address_and_block_count(self):
        for offset,value in [(512+28,0xe48bff56),(512+12,0x10200000),
                             (512+24,1),(512+20,1),(512+8,0xa000)]:
            with self.subTest(offset=offset):
                damaged=bytearray(self.original)
                struct.pack_into('<I',damaged,offset,value)
                with self.assertRaises(ValueError): self.inspect(damaged)

    def test_absolute_record_must_be_explicitly_ignored(self):
        for offset in [8,32,288]:
            with self.subTest(offset=offset):
                damaged=bytearray(self.original)
                damaged[offset]^=1
                with self.assertRaises(ValueError): self.inspect(damaged)

    def test_truncation_and_missing_last_block(self):
        for data in [self.original[:-1],self.original[:-512],self.original[:512]]:
            with self.assertRaises(ValueError): self.inspect(data)


if __name__=='__main__': unittest.main()
