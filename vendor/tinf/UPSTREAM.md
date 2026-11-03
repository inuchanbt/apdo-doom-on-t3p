# tinf analysis/runtime subset

Source: https://github.com/jibsen/tinf
Revision: `57ffa1f1d5e3dde19011b2127bd26d01689b694b`, retrieved 2026-10-31.
Version 1.2.1, Copyright (c) 2003-2019 Joergen Ibsen, zlib license.

Imported subset: `src/tinflate.c`, `src/tinf.h`, `LICENSE`.
Raw DEFLATE decoder only; gzip/zlib wrappers and checksum modules are not used.
The project wrapper validates its own page table and decoded-page CRC32.

V12 and later enable the local T3P_TINF_LOCAL_BITS decode path.
