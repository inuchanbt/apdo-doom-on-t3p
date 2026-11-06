"""Cross-check the native bounded LZ4 decoder with host python-lz4, offline."""
import argparse
import ctypes
from pathlib import Path
import random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=Path(
        'D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'))
    parser.add_argument('--lz4-module-path', type=Path, default=ROOT/'local/host-lz4')
    args = parser.parse_args()
    sys.path.insert(0, str(args.lz4_module_path.resolve()))
    import lz4.block
    output = ROOT/'artifacts/host-check'
    output.mkdir(parents=True, exist_ok=True)
    dll = output/'t3p-lz4.dll'
    subprocess.run([str(args.compiler), '-shared', '-O2', '-Wall', '-Wextra',
                    '-Werror', str(ROOT/'native/t3p_lz4.c'), '-o', str(dll)], check=True)
    lib = ctypes.CDLL(str(dll))
    decode = lib.t3p_lz4_decode
    decode.argtypes = (ctypes.c_void_p, ctypes.c_uint32,
                       ctypes.c_void_p, ctypes.c_uint32)
    decode.restype = ctypes.c_int

    def run(packed, length):
        source = ctypes.create_string_buffer(packed)
        # Check both ends, including output overflow attempts on malformed data.
        dest = ctypes.create_string_buffer(b'\xa5'*(length+32), length+32)
        result = decode(ctypes.byref(dest, 16), length, source, len(packed))
        assert dest.raw[:16] == b'\xa5'*16
        assert dest.raw[16+length:] == b'\xa5'*16
        return result, dest.raw[16:16+length]

    rng = random.Random(0x4c5a34)
    corpus = []
    for size in (0, 1, 4, 5, 12, 13, 31, 255, 256, 1024, 2048, 4096, 65536):
        corpus.extend((bytes(size), bytes(rng.randrange(256) for _ in range(size)),
                       bytes(rng.randrange(8) for _ in range(size)),
                       (bytes(range(256))*((size+255)//256))[:size]))
    for distance in range(1, 257):
        seed = bytes(rng.randrange(256) for _ in range(distance))
        corpus.append((seed*((2048+distance-1)//distance))[:2048])
    streams = 0
    for raw in corpus:
        for mode, level in (('default', 0), ('high_compression', 9), ('high_compression', 12)):
            packed = lz4.block.compress(raw, mode=mode, compression=level, store_size=False)
            assert lz4.block.decompress(packed, uncompressed_size=len(raw)) == raw
            result, restored = run(packed, len(raw))
            assert result == 0 and restored == raw
            if raw:
                assert run(packed, len(raw)-1)[0] != 0
            assert run(packed[:-1], len(raw))[0] != 0
            streams += 1
    invalid = (b'', b'\xf0', b'\xf0\xff', b'\x10a\x00\x00',
               b'\x00\x01\x00', b'\x1fa\x01\x00\xff', b'\xf0\xff\xff\xff\x00')
    for packed in invalid:
        assert run(packed, 2048)[0] != 0
    # Corruption may still form a valid block: require bounds safety, not rejection.
    for _ in range(5000):
        packed = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 129)))
        run(packed, rng.randrange(0, 2049))
    print(f'PASS: {streams} LZ4 streams; truncation/short output; invalid offsets/lengths; '
          '5000 malformed inputs with output canaries')


if __name__ == '__main__':
    main()
