# jl-misctools: local analysis subset

Source: https://github.com/kagaimiq/jl-misctools

Revision: `0a5b12db0ef38f3042acffbe2452730a37fd2405`.
Retrieved 2026-10-31. MIT license; see `LICENSE`.

Included: `firmware/fwunpack_newfw.py` and four `firmware/jltech` modules.

Local modifications:

- `crc.py`: replace the unpacker's CRC16 dependency with Python `binascii.crc_hqx`.
  The unused `jl_crc32` explicitly raises `NotImplementedError` in this subset.
- `fwunpack_newfw.py`: JSON serialization in place of PyYAML. The output retains
  its upstream `jlfw.yaml` name; JSON is a YAML subset.

The upstream CLI catches extraction errors and can still return exit code zero.
Check its log for `[!]` and run `scripts/verify_stock.py`; an exit code alone does
not prove that extraction succeeded.

These are offline tools. They do not implement device flashing.
