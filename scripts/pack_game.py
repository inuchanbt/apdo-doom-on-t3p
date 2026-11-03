"""Create a random-access, independently deflated 4KiB page game pack offline."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zlib

ROOT=Path(__file__).resolve().parents[1]
PAGE=4096
MAGIC=b'T3PGAME1'


def pack(raw,page_size=PAGE,raw_pages=(),try_strategies=False):
    if not 0<len(raw)<=0x400000:
        raise ValueError('Game pack must contain 1..4MiB')
    pages=[]
    for start in range(0,len(raw),page_size):
        block=raw[start:start+page_size]
        compressor=zlib.compressobj(9,zlib.DEFLATED,-15)
        compressed=compressor.compress(block)+compressor.flush()
        if try_strategies and start//page_size not in raw_pages:
            for strategy in (zlib.Z_FILTERED,zlib.Z_FIXED,zlib.Z_RLE):
                compressor=zlib.compressobj(9,zlib.DEFLATED,-15,8,strategy)
                candidate=compressor.compress(block)+compressor.flush()
                if len(candidate)<len(compressed):
                    compressed=candidate
        stored=compressed if start//page_size not in raw_pages and len(compressed)<len(block) else block
        pages.append((stored,len(stored)|(0x80000000 if stored is block else 0),len(block),zlib.crc32(block)))
    offset=64+16*len(pages)
    table=bytearray()
    payload=bytearray()
    for stored,flags,length,crc in pages:
        table+=struct.pack('<4I',offset,flags,length,crc)
        payload+=stored
        offset+=len(stored)
    header=struct.pack('<8s6I32s',MAGIC,1,len(raw),page_size,len(pages),offset,zlib.crc32(table),hashlib.sha256(raw).digest())
    return header+table+payload


def verify(image,raw,page_size=PAGE):
    magic,version,size,page,count,total,crc,digest=struct.unpack_from('<8s6I32s',image)
    if (magic,version,size,page,total,digest)!=(MAGIC,1,len(raw),page_size,len(image),hashlib.sha256(raw).digest()):
        raise ValueError('Header mismatch')
    table=image[64:64+16*count]
    if zlib.crc32(table)!=crc:
        raise ValueError('Table CRC mismatch')
    restored=bytearray()
    for offset,flags,length,crc in struct.iter_unpack('<4I',table):
        stored=image[offset:offset+(flags&0x7fffffff)]
        block=stored if flags&0x80000000 else zlib.decompress(stored,-15)
        if len(block)!=length or zlib.crc32(block)!=crc:
            raise ValueError('Page integrity failure')
        restored+=block
    if restored!=raw:
        raise ValueError('Roundtrip mismatch')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=Path('D:/doom_on_km003c/vendor/dmc100-doom/build/packs/E1M1/DOOM.DAT'))
    parser.add_argument('--output',type=Path,default=ROOT/'artifacts/game/E1M1.t3pg')
    args=parser.parse_args()
    raw=args.input.read_bytes()
    image=pack(raw)
    verify(image,raw)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(image)
    notices=args.output.parent/'notices'
    notices.mkdir(exist_ok=True)
    for source,name in [(ROOT/'notices/freedoom-0.13.0/COPYING.txt','freedoom-COPYING.txt'),
                        (ROOT/'notices/freedoom-0.13.0/CREDITS.txt','freedoom-CREDITS.txt'),
                        (ROOT/'COPYING','engine-COPYING'),
                        (ROOT/'vendor/dmc100-doom/CREDITS.md','engine-CREDITS.md')]:
        shutil.copyfile(source,notices/name)
    manifest=dict(format='T3PGAME1',version=1,page_bytes=PAGE,pages=(len(raw)+PAGE-1)//PAGE,
                  input_file=str(args.input.resolve()),input_bytes=len(raw),
                  input_sha256=hashlib.sha256(raw).hexdigest(),packed_bytes=len(image),
                  packed_sha256=hashlib.sha256(image).hexdigest(),
                  ratio=round(len(image)/len(raw),6),full_python_roundtrip=True,
                  status='Offline game asset only; no flash placement or device execution')
    args.output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__':
    main()
