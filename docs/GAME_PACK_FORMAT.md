# Random-access game pack

`T3PGAME1` is the eight-byte magic shared by versions 1 and 2. All integer
fields are little endian. Each page is independently readable and decoded to
the corresponding range in the original converted `DOOM.DAT`.

The 64-byte header contains magic, version, raw byte count, page byte count,
page count, total stored byte count, CRC32 of the page table, and SHA256 of
the complete raw data, in that order. Integer fields are 32 bits.

Each 16-byte table entry contains payload offset, stored byte count with
codec flags, decoded byte count, and decoded CRC32. Payloads are contiguous
in table order. Nonfinal pages decode to the configured page size; the last
page contains exactly the remaining raw bytes.

| Version | High bits of stored count | Encoding |
|---|---|---|
| 1 | `0x80000000` | Raw bytes; count equals decoded size |
| 1 | `0` | Raw DEFLATE stream; count is smaller than decoded size |
| 2 | `0x80000000` | Raw bytes; count equals decoded size |
| 2 | `0x40000000` | Independent raw LZ4 block, without a size prefix or frame header |
| 2 | `0` | Raw DEFLATE stream |

Version 2 uses the low 30 bits for stored size. Setting both codec bits is
invalid; compressed payloads must be nonempty and smaller than decoded data.
Builds with `T3P_GAME_LZ4` accept versions 1 and 2; older builds accept only
version 1. Each LZ4 block uses only its own decoded output as its dictionary.
The bounded native decoder follows the [LZ4 block specification](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md).

The native reader validates the table CRC and all payload ranges at open.
With `T3P_PROFILE_HOT`, each page's decoded CRC is checked on first use after
open. The backing image remains immutable. Raw pages retain direct mapped
Flash reads; compressed pages share the existing LRU cache and staging buffer.

V32 uses 2,048-byte pages and the V30 7-bit DEFLATE lookup. It converts raw
pages `349`, `007`, `547` to LZ4, saving 3,969 bytes, and DEFLATE pages `535`,
`537`, `538`, `544` to LZ4, costing 2,141 bytes. Page numbers here are hex.
The resulting asset pack is 1,247,309 bytes, 1,828 fewer than V30. Other
page payloads retain their V30 encoding and contents. The 35-page cache,
120x120 indexed framebuffer, DBI path and left-button input are unchanged.

This trades direct reads on three pages for cheaper decoding on four others;
the total device performance is an experiment, not an established speedup.

For V32, install the host-only `lz4==4.4.4` package under `local/host-lz4`
(or use `--lz4-module-path`) and build with `--revision 32`. The native
decoder is project code; no Python dependency is loaded on the device.
`scripts/check_lz4_decoder.py` checks it against host LZ4 output, including
overlap copies, truncated streams, short output buffers and malformed inputs.
`tests/game_pack.c` checks every page and 5,000 random reads, and validates
version/codec rejection when compiled with `T3P_GAME_LZ4`.
