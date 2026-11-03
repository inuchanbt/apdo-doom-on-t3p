""" CRC calculation routines """

__all__ = [
    'jl_crc16',
    'jl_crc32'
]

# Local adaptation: use the standard library for the unpacker's CRC16.
from binascii import crc_hqx

def jl_crc16(data, crc=0):
    return crc_hqx(data, crc)

def jl_crc32(data, crc=None):
    raise NotImplementedError('CRC32 is not used by this local unpacker subset')

