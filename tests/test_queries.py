"""Offline checks for the only two allowed packets and response framing."""
import sys
from pathlib import Path
import unittest
import struct
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from t3p_queries import request, payload, describe, query


class Queries(unittest.TestCase):
    def test_requests(self):
        for kind,header in [('settings','55050903'),('measurement','55052205')]:
            report=request(kind)
            self.assertEqual(len(report),65)
            self.assertEqual(report[0],0)
            self.assertEqual(report[1:5],bytes.fromhex(header))
            self.assertNotEqual(report[1:5],bytes.fromhex('55050902'))
        for kind in ['update','reset','motor','write','55050902']:
            with self.assertRaises(ValueError): request(kind)

    def test_framing(self):
        data=bytearray(64)
        data[:4]=bytes.fromhex('aa054003'); data[-2:]=b'\xee\xff'
        data[8]=1; data[9]=42; data[13]=0
        self.assertEqual(payload(data),payload(b'\0'+data))
        self.assertEqual(describe(data)['raw_settings'],dict(byte_8=1,byte_9=42,byte_13=0))
        self.assertEqual(describe(b'\0'+data)['type'],'settings')
        data[-1]=0
        self.assertEqual(describe(data)['type'],'unclassified')
        for bad in [b'',bytes(63),bytes(66),b'\x55'+bytes(64)]:
            with self.assertRaises(ValueError): payload(bad)

    def test_measurement(self):
        data=bytearray(64)
        data[:4]=bytes.fromhex('aa406205')
        for n in range(8,52,4):
            struct.pack_into('<I',data,n,1000000+n)
        data[60]=2
        decoded=describe(data)
        self.assertEqual(decoded['type'],'measurement')
        self.assertEqual(decoded['scaled_fields']['offset_08'],1.000008)
        self.assertEqual(decoded['scaled_fields']['offset_30'],1.000048)
        self.assertEqual(decoded['byte_60'],2)
        # A measurement report does not use the settings response's EE FF tail.
        self.assertEqual(data[-2:],bytes(2))

    def test_transport_whitelist_and_failures(self):
        data=bytearray(64)
        data[:4]=bytes.fromhex('aa054003'); data[-2:]=b'\xee\xff'
        info=dict(path=b'test-only',usage_page=0xff02,usage=2,
                  product_string='ATORCH T3P V3.0.2')
        class Fake:
            def __init__(self,reply,short=False):
                self.reply=reply; self.short=short; self.writes=[]; self.closed=False
            def open_path(self,path):
                assert path==b'test-only'
            def read(self,size,timeout):
                assert size==65 and timeout>0
                return self.reply if self.writes else []
            def write(self,packet):
                self.writes.append(packet)
                return len(packet)-1 if self.short else len(packet)
            def close(self): self.closed=True
        for reply,short in [(data,False),(bytes(64),False),(data,True)]:
            fake=Fake(reply,short)
            module=SimpleNamespace(enumerate=lambda v,p:[info],device=lambda:fake)
            with patch.dict(sys.modules,{'hid':module}),patch('t3p_queries.time.sleep'):
                if short:
                    with self.assertRaises(RuntimeError): query('settings',0.1,2)
                else:
                    result=query('settings',0.1,2)
                    self.assertEqual(result['success'],reply==data)
            self.assertTrue(fake.closed)
            self.assertTrue(all(packet==request('settings') for packet in fake.writes))
            self.assertEqual(len(fake.writes),2 if reply==data and not short else 1)
        module=SimpleNamespace(enumerate=lambda v,p:[dict(info,product_string='Other device')],
                               device=lambda:self.fail('Opened an unrecognized device'))
        with patch.dict(sys.modules,{'hid':module}):
            with self.assertRaises(RuntimeError): query('settings',0.1)


if __name__=='__main__':
    unittest.main()
