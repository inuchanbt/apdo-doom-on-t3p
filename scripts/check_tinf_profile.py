"""Offline V41/V42 raw-timer, sampling and decoder-equivalence checks."""
import argparse
import ctypes
from pathlib import Path
import struct
import subprocess
import zlib
from check_tinf_prefix import streams

ROOT=Path(__file__).resolve().parents[1]
HOST=r'''#include <stdint.h>
#include "t3p_tinf_profile.h"
static uint32_t tick, reads;
uint32_t t3p_tinf_clock(void) { reads++; return tick++ & 0xffffu; }
void probe_clock_set(uint32_t value) { tick=value;reads=0; }
uint32_t probe_clock_reads(void) { return reads; }
uint32_t probe_begin(unsigned bucket) { return t3p_tinf_sample_begin(bucket); }
void probe_end(unsigned bucket,uint32_t start) { t3p_tinf_sample_end(bucket,start); }
'''


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,default=Path('D:/doom_on_km003c/tools/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'))
    p.add_argument('--packed',type=Path,required=True)
    args=p.parse_args()
    out=ROOT/'artifacts/host-check';out.mkdir(exist_ok=True,parents=True)
    source=out/'tinf-profile-host.c';source.write_text(HOST,encoding='utf-8')
    corpus=list(streams())
    image=args.packed.read_bytes()
    magic,version,size,page,count,total,crc,digest=struct.unpack_from('<8s6I32s',image)
    assert magic==b'T3PGAME1' and version==1 and total==len(image)
    for offset,flags,length,_ in struct.iter_unpack('<4I',image[64:64+16*count]):
        if flags&0xc0000000:continue
        packed=image[offset:offset+flags];raw=zlib.decompress(packed,-15)
        assert len(raw)==length
        corpus.append((raw,packed))
    for sampled in (False,True):
        dll=out/f'tinf-profile-{41+int(sampled)}.dll'
        subprocess.run([str(args.compiler),'-shared','-O2','-Wall','-Wextra','-Werror',
            '-DNDEBUG','-DT3P_TINF_LOCAL_BITS','-DT3P_TINF_PREFIX',
            '-DT3P_TINF_PREFIX_CONTINUE','-DT3P_TINF_HOT_SECTION',
            '-DT3P_TINF_PROFILE','-DT3P_TINF_PROFILE_HOST',
            *(['-DT3P_TINF_PROFILE_SAMPLES'] if sampled else []),
            '-I',str(ROOT/'native'),'-I',str(ROOT/'vendor/tinf'),
            str(ROOT/'vendor/tinf/tinflate.c'),str(ROOT/'native/t3p_tinf_profile.c'),
            str(source),'-o',str(dll)],check=True)
        lib=ctypes.CDLL(str(dll));stats=(ctypes.c_uint32*16).in_dll(lib,'t3p_tinf_profile')
        lib.probe_clock_set.argtypes=[ctypes.c_uint32]
        lib.probe_begin.argtypes=[ctypes.c_uint];lib.probe_begin.restype=ctypes.c_uint32
        lib.probe_end.argtypes=[ctypes.c_uint,ctypes.c_uint32]
        lib.t3p_tinf_charge.argtypes=[ctypes.c_uint,ctypes.c_uint32]
        lib.t3p_tinf_profile_reset();lib.probe_clock_set(3)
        lib.t3p_tinf_charge(0,65534)
        assert stats[0]==5,'16-bit wrap delta'
        lib.t3p_tinf_profile_reset();lib.probe_clock_set(0)
        for call in range(129):
            start=lib.probe_begin(7)
            assert bool(start)==(sampled and call%64==0)
            lib.probe_end(7,start)
        assert stats[7]==129 and stats[9]==(3 if sampled else 0)
        assert stats[8]==stats[13]==stats[14]==(3 if sampled else 0)
        assert lib.probe_clock_reads()==(12 if sampled else 0)
        assert stats[15]==(6 if sampled else 0)
        # Counter-zero samples must not be mistaken for "not sampled".
        lib.t3p_tinf_profile_reset();lib.probe_clock_set(65534)
        start=lib.probe_begin(10)
        if sampled:assert start==0x10000
        lib.probe_end(10,start)
        assert stats[12]==int(sampled) and stats[11]==int(sampled)
        decode=lib.tinf_uncompress
        decode.argtypes=(ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint),ctypes.c_void_p,ctypes.c_uint)
        decode.restype=ctypes.c_int
        for index,(raw,packed) in enumerate(corpus):
            lib.t3p_tinf_profile_reset();lib.probe_clock_set(65000)
            src=ctypes.create_string_buffer(packed);dst=ctypes.create_string_buffer(max(1,len(raw)))
            length=ctypes.c_uint(len(raw))
            rc=decode(dst,ctypes.byref(length),src,len(packed))
            assert rc==0 and length.value==len(raw) and dst.raw[:len(raw)]==raw,index
            assert stats[3]==1 and stats[4]+stats[5]+stats[6]>0,index
            assert bool(stats[0])==bool(stats[4]+stats[5]),index
            assert bool(stats[1])==bool(stats[4]+stats[5]),index
            assert bool(stats[2])==bool(stats[6]),index
            for calls,ticks,samples in ((7,8,9),(10,11,12)):
                expected=(stats[calls]+63)//64 if sampled else 0
                assert stats[samples]==expected and stats[ticks]==expected,index
            assert stats[13]==stats[14]==stats[9]+stats[12],index
        # Safe early errors must still charge their completed phase interval.
        for packed,bucket in ((b'\x07',None),(b'\x01\x03\x00\x00\x00abc',2),
                              (b'\xfd\x00\x00\x00',0)):
            lib.t3p_tinf_profile_reset();lib.probe_clock_set(0)
            src=ctypes.create_string_buffer(packed);dst=ctypes.create_string_buffer(8);length=ctypes.c_uint(8)
            assert decode(dst,ctypes.byref(length),src,len(packed))!=0
            assert stats[3]==1
            if bucket is not None:assert stats[bucket]>0
        lib.t3p_tinf_profile_reset()
        assert list(stats)==[0]*15+[6 if sampled else 0]
        print(f'PASS V{41+int(sampled)}: timer wrap/zero/reset/sampling/error accounting; {len(corpus)} streams match zlib')


if __name__=='__main__':main()
