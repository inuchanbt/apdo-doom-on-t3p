"""Verify stock query dispatch and HID report framing, offline for all 4 files."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
EXPECTED={
    'ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c':0xc0cb48c,
    '707f64a0c36e854b1d20cfdaf5ab37601baf97b18043d70197b55da95079cd50':0xc0c5908,
    '2aeb41433f2d5b4b9da74af71c9cb7d4bbf7e8caf2d016d600c5f2cdb40c5254':0xc0c6218,
    'f2ca474c37b8ecc0403837d986d66428ebd8530f762db41cf24ad35881764cda':0xc0c6bbc,
}
DESCRIPTOR=bytes.fromhex('06 02 ff 09 02 a1 01 09 03 15 00 26 ff 00 75 08 95 40 81 00 09 04 15 00 26 ff 00 75 08 95 40 91 00 c0')


def main():
    results=[]
    for path in sorted((ROOT/'local/unpacked').glob('*/files/app.bin')):
        data=path.read_bytes()
        digest=hashlib.sha256(data).hexdigest()
        if digest not in EXPECTED:
            raise ValueError(f'Unsupported application: {path}')
        start=EXPECTED[digest]-0xc000100
        checks={
            'dispatcher_prefix':(0,'79041416484001ff5500'),
            'header_05_check':(12,'484101ff0500'),
            'command_22_check':(28,'02f81d44'),
            'command_09_check':(42,'01ff09200f01'),
            'update_subcommand_02_check':(48,'4a4382f8b204'),
            'update_tag':(60,'c2ff75626f6fc3ff745a7374'),
            'settings_subcommand_03_check':(0x19a,'484380f83b06'),
            'settings_response_header':(0x1a8,'6b2acb404325cb415320cb424323cb43'),
            'settings_response_tail':(0x202,'782e52ee4e03783f52ee4f03'),
        }
        if digest=='2aeb41433f2d5b4b9da74af71c9cb7d4bbf7e8caf2d016d600c5f2cdb40c5254':
            # The repaired 3.0.2 variant uses a longer branch and different
            # register allocation. Verify its own instructions explicitly.
            checks.update({
                'command_09_check':(42,'01ff09202201'),
                'update_subcommand_02_check':(48,'4a4382f8b304'),
                'settings_subcommand_03_check':(0x19c,'484380f84206'),
                'settings_response_header':(0x1b2,'682ac8404025c8415020c8424023c843'),
                'settings_response_tail':(0x212,'782e52ee4e03783f52ee4f03'),
            })
        for name,(offset,raw) in checks.items():
            expected=bytes.fromhex(raw)
            if data[start+offset:start+offset+len(expected)]!=expected:
                raise ValueError(f'{path.parent.parent.name}: {name} mismatch at +{offset:#x}')
        at=data.find(DESCRIPTOR)
        if at<0 or data.count(DESCRIPTOR)!=1:
            raise ValueError('Expected one exact unnumbered HID descriptor')
        results.append(dict(file=path.parent.parent.name,app_sha256=digest,
            dispatcher_address=hex(EXPECTED[digest]),checked_fragments=checks,
            descriptor_address=hex(0xc000100+at),descriptor_hex=DESCRIPTOR.hex(),
            report_id=None,payload_bytes=64,hidapi_write_bytes=65,hidapi_dummy_id=0,
            read_only_settings_command='55050903',
            measurement_command='55052205',
            update_command='55050902',update_tag_ascii='ubootZst'))
    if len(results)!=4:
        raise ValueError('Expected four known stock applications')
    output=ROOT/'analysis/hid-protocol.json'
    output.write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('PASS: all 4 stock parsers, settings reply and unnumbered 64-byte HID descriptors')


if __name__=='__main__':
    main()
