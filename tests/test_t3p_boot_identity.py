"""Real stock descriptor profiles; no USB devices opened."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from watch_t3p_boot_identity import classify,public_identity

class Identity(unittest.TestCase):
    def test_same_vid_pid_has_two_distinct_usage_profiles(self):
        base=dict(vendor_id=0x0483,product_id=0x5750)
        self.assertEqual(classify(dict(base,usage_page=0xff02,usage=2)),'stock_application_profile')
        self.assertEqual(classify(dict(base,usage_page=0xff00,usage=1)),'stock_HID_bootloader_profile')
        self.assertEqual(classify(dict(base,usage_page=0xff00,usage=2)),'unclassified_same_vid_pid')
        self.assertIsNone(classify(dict(base,product_id=0x5751,usage_page=0xff00,usage=1)))

    def test_enumeration_record_excludes_private_path_and_serial(self):
        row=dict(vendor_id=0x0483,product_id=0x5750,usage_page=0xff00,usage=1,
                 serial_number='private',path=b'privatepath',product_string='test',manufacturer_string='test')
        result=public_identity(row)
        self.assertNotIn('serial_number',result);self.assertNotIn('path',result)
        self.assertEqual(result['mode'],'stock_HID_bootloader_profile')

if __name__=='__main__':unittest.main()
