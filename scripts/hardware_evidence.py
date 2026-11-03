"""Verify and export version-specific hardware clues from stock 3.0.3.

Offline only. This is an analysis report, never a board driver or flashing map.
"""
import hashlib
import json
from pathlib import Path
import struct
from find_stock_calls import instructions
from stock_data import ROOT, load, read

EXPECTED_APP='ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c'


def main():
    data=load()
    digest=hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_APP:
        raise SystemExit('Unsupported application image; do not reuse these addresses')
    code={address:insn.strip().split('#')[0].strip()
          for address,_,insn,_ in instructions(ROOT/'analysis/app-3.0.3.disasm.txt')}
    evidence=[]

    def require(address, fragment):
        actual=code.get(address,'')
        if fragment not in actual:
            raise ValueError(f'Disassembly mismatch at {address:#x}: {actual!r}')
        evidence.append(dict(address=f'{address:#010x}',instruction=actual))

    def words(address,count):
        return list(struct.unpack('<'+'I'*count,read(data,address,count*4)))

    drive=words(0x11f678,14)
    name=read(data,drive[0],32).split(b'\0')[0].decode('ascii')
    param=words(drive[6],0x40//4)
    cfg=words(0xc111a08,2)
    pins=words(cfg[1],5)
    i2c=words(0xc111a10,7)
    init=read(data,0xc15a1ec,16)
    checks=[name=='st7789v' and cfg[0]==2 and drive[6]==0x120164,
            pins==[0x20,0xffffffff,0xffffffff,0xe7,0xffffffff],
            i2c[:6]==[0,6,5,0x11,0,100000],
            list(init)==[0x57,0,0x1f,0,0x20,0x3f,0x21,0x70,0x23,0x10,0x24,0x40,0x2e,0x8f,0x22,0]]
    if not all(checks):
        raise ValueError('Hardware table mismatch')
    require(0xc0708b6,'r0 = 0xc059a86')
    require(0xc0708bc,'r1 = 0xc111a08')
    require(0xc059d40,'r0 = r8')
    require(0xc059d42,'call r1')
    require(0xc0015cc,'r4 = 0x4')
    require(0xc0015ce,'r5 = 0xfd3008')
    require(0xc00166e,'r0 = 0xfd3108')
    require(0xc001674,'[r0+0xc] |= 0x100')
    require(0xc0cf8d4,'r0 = 0x100')
    require(0xc0cf8e2,'r0 = r5 + 0x10')
    require(0xc0cf4a6,'r2 = 0x2a01')
    require(0xc001706,'r1 = sp + 3')
    require(0xc001708,'r2 = 0x5')
    require(0xc00eba6,'r0 = 0x32')
    require(0xc0e6ce6,'r0 = 0xf')
    require(0xc0e6cf4,'if (r0 != 0x11)')
    require(0xc011c4a,'r0 = 0xa8')
    require(0xc011c4c,'r2 = 0x6')
    require(0xc011c60,'r1 = r1 >>> 0x4')
    require(0xc0e6d7e,'r0 = 0xee')
    require(0xc0e6d74,'r0 = 0xd0')
    require(0xc0e6d8e,'if (r0 != 0x58)')
    require(0xc0e6d96,'r0 = 0x88')
    require(0xc0e6dbe,'if (r5 != 0x18)')
    require(0xc0e6a6a,'r0 = 0xe8')
    require(0xc005504,'if (r0 != 0xe7)')

    # Marker-delimited LCD command records, including delay records.
    stream=read(data,drive[2],drive[3])
    start,end=bytes.fromhex('12345678'),bytes.fromhex('87654321')
    packets=[]
    cursor=0
    while cursor<len(stream):
        if stream[cursor:cursor+4]!=start:
            raise ValueError('Bad LCD command record')
        stop=stream.index(end,cursor+4)
        payload=stream[cursor+4:stop]
        packets.append(payload.hex())
        cursor=stop+4
    result=dict(
        status='OFFLINE EVIDENCE; physical chip identities, pins and sensor values not tested',
        firmware='T3P Firmware Version 3.0.3.bin',app_sha256=digest,
        connected_version_when_investigated='3.0.2 (addresses are NOT transferable)',
        lcd=dict(driver=name,registration_ram='0x0011f678',parameter_ram=f'{drive[6]:#010x}',
                 configured_size=[param[2],param[3]],input_size=[param[9],param[10]],
                 buffers=param[7],buffer_bytes=param[8],fps=param[15],
                 reset_gpio='PC00',backlight='IO_LCD_PG (0xe7), special output; not GPIO E7',
                 command_records=packets,
                 unresolved='Init 2A/2B window ends at 239, while parameter dimensions are 320; physical panel size unconfirmed'),
        i2c=dict(scl='PA06',sda='PA05',configured_hz=i2c[5],
                 confidence='configuration plus hardware initialization routing'),
        touch=dict(driver_family='CST816D',address_7bit='0x15',reset_gpio='PA02',irq_gpio='PB08',
                   data_register='0x02',read_bytes=5,coordinates='12-bit X/Y; orientation unconfirmed'),
        accelerometer=dict(driver_family='SC7A20-compatible',address_7bit='0x19',
                           id_register='0x0f',expected_id='0x11',data_register='0xa8',read_bytes=6,
                           encoding='little-endian signed 16-bit shifted right 4',
                           init_pairs=[list(init[i:i+2]) for i in range(0,16,2)],
                           confidence='ID, FIFO/data decoding and all eight init pairs match official SDK SC7A20 family implementation'),
        pressure=dict(driver_family='BMP280-compatible',address_7bit='0x77',
                      id_register='0xd0',expected_id='0x58',calibration_start='0x88',
                      calibration_bytes=24,data_registers='0xf7..0xfc',
                      confidence='ID, calibration and pressure/temperature register layout match Bosch BMP280'),
        motor=dict(output='IO_MT_PG (0xe8)',confidence='special PWM-output initialization and SDK identifier; real vibration not tested'),
        microphone=dict(status='ADC/audio path and channel not yet located'),
        evidence=evidence)
    output=ROOT/'analysis/hardware-3.0.3.json'
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'PASS: {len(evidence)} instruction checks, LCD/I2C tables and accelerometer init; {output}')


if __name__=='__main__':
    main()
