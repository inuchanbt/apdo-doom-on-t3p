"""Offline checks for the optional 7/8-bit decoder against Python zlib."""
import argparse
import ctypes
from pathlib import Path
import random
import subprocess
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]


def streams():
    rng = random.Random(0x7331)
    strategies = (zlib.Z_DEFAULT_STRATEGY, zlib.Z_FIXED,
                  zlib.Z_HUFFMAN_ONLY, zlib.Z_RLE)
    for size in (0, 1, 7, 31, 257, 1024, 2048, 16384, 32768):
        data_sets = (
            bytes(size),
            bytes(rng.randrange(256) for _ in range(size)),
            bytes(rng.randrange(8) for _ in range(size)),
            (bytes(range(256)) * ((size + 255) // 256))[:size],
            (b'abacabadabacaba!' * ((size + 14) // 15))[:size],
        )
        for raw in data_sets:
            for strategy in strategies:
                encoder = zlib.compressobj(6, zlib.DEFLATED, -15, 8, strategy)
                yield raw, encoder.compress(raw) + encoder.flush()
                # A sync flush inserts an empty stored block. Exercise all
                # nearby split points, including partial Huffman bytes before
                # that stored block and the following compressed block.
                for split in sorted({0, min(1, size), size // 2, size}):
                    encoder = zlib.compressobj(6, zlib.DEFLATED, -15, 8, strategy)
                    packed = encoder.compress(raw[:split])
                    packed += encoder.flush(zlib.Z_SYNC_FLUSH)
                    packed += encoder.compress(raw[split:]) + encoder.flush()
                    yield raw, packed
    # A complete, deliberately skewed tree reaches DEFLATE's 15-bit
    # maximum. Vary leading one-bit literals to shift those long codes
    # across byte boundaries; zlib-generated samples need not reach 15 bits.
    for leading in range(8):
        bits=[]
        def put(value,width):
            bits.extend((value>>i)&1 for i in range(width))
        def code(value,width):
            bits.extend((value>>i)&1 for i in range(width-1,-1,-1))
        put(1,1);put(2,2)  # final dynamic block
        put(0,5);put(0,5);put(15,4)  # 257 literals, one distance, 19 lengths
        order=(16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15)
        for symbol in order:put(4 if symbol<16 else 0,3)
        lengths=[0]*258
        for symbol in range(15):lengths[symbol]=symbol+1
        lengths[256]=15;lengths[257]=1
        for length in lengths:code(length,4)  # code-length alphabet: 0..15
        raw=bytes(leading)+bytes(range(15))*2
        for symbol in raw:
            width=symbol+1
            code((1<<width)-2,width)
        code(32767,15)  # EOB
        packed=bytes(sum(bit<<j for j,bit in enumerate(bits[i:i+8]))
                     for i in range(0,len(bits),8))
        yield raw,packed
    # Forward overlap copies with distances 1..64 and longer repetitions.
    for distance in range(1, 65):
        seed = bytes(rng.randrange(256) for _ in range(distance))
        raw = (seed * ((4096 + distance - 1) // distance))[:4096]
        encoder = zlib.compressobj(9, zlib.DEFLATED, -15)
        yield raw, encoder.compress(raw) + encoder.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=Path(
        'D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'))
    parser.add_argument('--packed', type=Path, help='Also check every DEFLATE page in a T3PGAME1 pack')
    args = parser.parse_args()
    output = ROOT / 'artifacts/host-check'
    output.mkdir(parents=True, exist_ok=True)
    corpus = list(streams())
    if args.packed:
        image=args.packed.read_bytes()
        magic,version,size,page,count,total,crc,digest=struct.unpack_from('<8s6I32s',image)
        assert magic==b'T3PGAME1' and version in (1,2) and total==len(image)
        for offset,flags,length,_ in struct.iter_unpack('<4I',image[64:64+16*count]):
            if flags&0xc0000000:continue
            packed=image[offset:offset+flags]
            raw=zlib.decompress(packed,-15)
            assert len(raw)==length
            corpus.append((raw,packed))
    for bits,continuation,hot,literals in ((7,False,False,False),(8,False,False,False),(7,True,False,False),(8,True,False,False),(7,True,True,False),(7,True,True,True)):
        mode=('continue' if continuation else 'restart')+('-hot' if hot else '')+('-literals' if literals else '')
        dll = output / f'tinf-prefix-{bits}-{mode}.dll'
        subprocess.run([
            str(args.compiler), '-shared', '-O2', '-Wall', '-Wextra', '-Werror',
            '-DNDEBUG', '-DT3P_TINF_LOCAL_BITS', '-DT3P_TINF_PREFIX',
            *(['-DT3P_TINF_PREFIX_CONTINUE'] if continuation else []),
            *(['-DT3P_TINF_HOT_SECTION'] if hot else []),
            *(['-DT3P_TINF_LITERAL_RUN'] if literals else []),
            f'-DT3P_TINF_PREFIX_BITS={bits}', '-I', str(ROOT / 'vendor/tinf'),
            str(ROOT / 'vendor/tinf/tinflate.c'), '-o', str(dll),
        ], check=True)
        lib = ctypes.CDLL(str(dll))
        decode = lib.tinf_uncompress
        decode.argtypes = (ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint),
                           ctypes.c_void_p, ctypes.c_uint)
        decode.restype = ctypes.c_int
        for index, (raw, packed) in enumerate(corpus):
            assert zlib.decompress(packed, -15) == raw
            source = ctypes.create_string_buffer(packed)
            dest = ctypes.create_string_buffer(max(1, len(raw)))
            length = ctypes.c_uint(len(raw))
            result = decode(dest, ctypes.byref(length), source, len(packed))
            if result or length.value != len(raw) or dest.raw[:len(raw)] != raw:
                raise RuntimeError(f'{bits}-bit {mode} lookup failed on stream {index}')
        if literals:
            # Every stream also exercises a too-small destination, with canaries.
            # A full destination must still allow an immediately following EOB.
            for raw,packed in corpus:
                if not raw:continue
                for capacity in (0,len(raw)//2,len(raw)-1):
                    source=ctypes.create_string_buffer(packed)
                    guard=ctypes.create_string_buffer(b'\xa5'*(capacity+2))
                    length=ctypes.c_uint(capacity)
                    result=decode(ctypes.addressof(guard)+1,ctypes.byref(length),source,len(packed))
                    assert result!=0 and guard.raw[0]==guard.raw[capacity+1]==0xa5
            print('PASS: literal-run destination bounds/canaries for every nonempty stream')
        print(f'PASS: {bits}-bit {mode} prefix, {len(corpus)} zlib/overlap/block-transition/pack streams')


if __name__ == '__main__':
    main()
