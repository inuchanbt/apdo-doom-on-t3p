"""Export SHA-gated, offline memory and clock clues; never opens the device."""
import hashlib
import json
import re
import struct
from find_stock_calls import instructions
from stock_data import ROOT, load, read

EXPECTED_APP = 'ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c'


def main():
    data = load()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_APP:
        raise SystemExit('Unsupported stock application; addresses are version-specific')
    wanted = {
        0xc000120: 'r3 = 0x101a80', 0xc000128: 'r2 = 0x1d660',
        0xc000136: 'r4 = 0x11f0e0', 0xc000142: 'r2 = 0x1b80',
        0xc000152: 'r4 = 0x120c60', 0xc00015e: 'r2 = 0x2740',
        0xc010178: 'r3 = 0x1233a0', 0xc01017e: 'r4 = 0x136e00',
        0xc010906: 'r0 = 0x374208', 0xc01090c: 'r1 = 0x378000',
        0xc010924: 'r0 = 0x3c4000', 0xc01092a: 'r1 = 0x3c8000',
        0xc000194: 'r0 = 0x7a8', 0xc000198: 'r1 = 0x101c00',
        0xc00019e: 'r0 = b[r1+r0] (u)', 0xc0001a2: 'if (r0 == 0x0)',
        0xc0001a6: 'r0 = 0x7a12000', 0xc0001ac: 'r0 = 0x112a8800',
        0xc00027a: 'r1 = 0x29a8', 0xc00027e: 'r5 = 0xc158d20',
    }
    evidence = []
    for address, raw, instruction, _ in instructions(ROOT/'analysis/app-3.0.3.disasm.txt'):
        if address not in wanted:
            continue
        if wanted[address] not in instruction or read(data, address, len(raw)) != raw:
            raise ValueError(f'Instruction or image mismatch: {address:#x}')
        evidence.append(dict(address=f'{address:#010x}', raw_hex=raw.hex(),
                             instruction=instruction.strip()))
    if len(evidence) != len(wanted):
        raise ValueError('Missing instructions in the offline disassembly')
    sdk = ROOT/'local/jieli-sdk/SDK'
    paths = ['cpu/br35/maskrom_stubs.ld', 'cpu/br35/sdk_ld.c',
             'interface/driver/cpu/br35/asm/power/p11/p11_mmap.h']
    texts = {p: (sdk/p).read_text(encoding='utf-8', errors='replace') for p in paths}
    limits = [int(re.search(r'_RAM_LIMIT_'+side+r'\s*=\s*ABSOLUTE\((0x[0-9a-f]+)\)',
                            texts[paths[0]]).group(1), 16) for side in ['L', 'H']]
    if limits != [0x100000, 0x137000] or '0x370000' not in texts[paths[1]] or '0x3C0000' not in texts[paths[1]]:
        raise ValueError('SDK memory map changed')
    if not re.search(r'#define P11_RAM_SIZE\s+\(0x8000\)', texts[paths[2]]):
        raise ValueError('SDK Sensor Hub memory size changed')
    table = list(struct.unpack('<9I', read(data, 0xc15b6c8, 36)))
    if table != [96000000,48000000,96000000,192000000,64000000,96000000,288000000,96000000,96000000]:
        raise ValueError('Stock clock selection table changed')
    result = dict(
        status='OFFLINE EVIDENCE; not a physical RAM test or a clock measurement',
        application_version='3.0.3', application_sha256=digest,
        family='AC707N / BR35', exact_device_part_number='unconfirmed',
        sdk_sources={p: hashlib.sha256((sdk/p).read_bytes()).hexdigest() for p in paths},
        main_sram=dict(begin=hex(limits[0]), end_exclusive=hex(limits[1]), bytes=limits[1]-limits[0]),
        cache=dict(instruction_bytes=32768, data_bytes=32768, bytes_per_way=8192,
                   counted_in_284_KiB_total=True, reclaim_reduces_cache=True),
        main_plus_cache_bytes=limits[1]-limits[0]+65536,
        sensor_hub=dict(bytes=32768, separate_address_space=True, included_in_doom_budget=False),
        stock_initial_heaps=[dict(begin=hex(a),end_exclusive=hex(b),bytes=b-a)
                             for a,b in [(0x1233a0,0x136e00),(0x374208,0x378000),(0x3c4000,0x3c8000)]],
        stock_clock=dict(conditional_limit_hz=[128000000,288000000],
                         unlabelled_selection_rows_hz=[table[i:i+3] for i in range(0,9,3)],
                         current_frequency_hz=None,
                         caveat='Limits and selection options are not the current operating frequency'),
        psram='not established; generic strings and virtual mappings are insufficient',
        instruction_evidence=evidence)
    output = ROOT/'analysis/soc-3.0.3.json'
    output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Verified', len(evidence), 'stock instructions; main + cache:', result['main_plus_cache_bytes'], 'bytes')
    print('Clock limit branches: 128 / 288 MHz; actual running clock remains unknown')


if __name__ == '__main__':
    main()
