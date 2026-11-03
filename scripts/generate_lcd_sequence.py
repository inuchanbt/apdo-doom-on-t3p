"""Generate the exact stock3.0.3 panel command sequence, offline."""
import hashlib
from pathlib import Path
import struct
from stock_data import ROOT,load,read


def generate():
    image=load()
    assert hashlib.sha256(image).hexdigest()=='ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c'
    driver=struct.unpack('<14I',read(image,0x11f678,56))
    stream=read(image,driver[2],driver[3])
    start,end=bytes.fromhex('12345678'),bytes.fromhex('87654321')
    cursor=0;records=[]
    while cursor<len(stream):
        assert stream[cursor:cursor+4]==start
        stop=stream.index(end,cursor+4)
        records.append(stream[cursor+4:stop]);cursor=stop+4
    # The control-flow parser in stock uses ff5aa5ff followed by one delay byte.
    assert len(records)==23 and all(records)
    out=['/* Generated from exact stock3.0.3 ST7789V initialization records. */',
         '#include "t3p_bsp.h"','int t3p_bsp_lcd_sequence(struct t3p_bsp *b)','{']
    for i,r in enumerate(records):
        if r.startswith(bytes.fromhex('ff5aa5ff')):
            assert len(r)==5
            out.append(f'    if(t3p_bsp_delay(b,{r[4]}))return -2;')
        else:
            if len(r)>1:
                out.append('    static const uint8_t data'+str(i)+'[]={'+','.join(hex(x) for x in r[1:])+'};')
            out.append(f'    if(t3p_bsp_command(b,0x{r[0]:02x},'+(f'data{i}' if len(r)>1 else '0')+f',{len(r)-1}))return -2;')
    out+=['    return 0;','}','']
    path=ROOT/'native/t3p_lcd_sequence.c';path.write_text('\n'.join(out),encoding='utf-8')
    return len(records),hashlib.sha256(stream).hexdigest()


if __name__=='__main__':print('Generated stock LCD records:',generate())
