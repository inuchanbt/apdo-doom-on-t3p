"""Build a bounded two-function drawing probe using the exact stock 3.0.3 image.

Creates a reviewable update file, never opens a USB device or runs an updater.
This is a stock-based native drawing experiment, not standalone DOOM/startup.
"""
import binascii
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from audit_stock_recovery import entry, require
from check_stock_app_container import rebuild_in_memory
from find_stock_calls import instructions
from stock_data import BASE, ROOT, read
from jltech.cipher import jl_enc_cipher

STOCK = '72571d747e9da86194deddae3802992f846415c45b82d7b28f8aa567af30acb2'
APP = 'ecd03d659db7c961585ffd1e090a7325a82433a1c270f5964f268ddb5c1bdc0c'
SLOTS = {
    '.probe_draw': (0x0c00d638, 0x90,
        'a1e8216f22d87fec6496cc9e9ee786877ea5b793c0fe87e296088e835702fb80'),
    '.probe_continue': (0x0c01106a, 0xd0,
        '0a1389363d65d8b014121df37ab0b6a05871341d6596836e17e1544488697529'),
}
EXPECTED_CALLS = {
    '.probe_draw': [0x0c006ec6, 0x0c02ec4e],
    '.probe_continue': [0x0c006ec6, 0x0c006efa, 0x0c02ec96],
}

def digest(data): return hashlib.sha256(data).hexdigest()

def elf_sections(data):
    require(data[:6] == b'\x7fELF\x01\x01', 'ELF format')
    require(struct.unpack_from('<H', data, 18)[0] == 0xf1, 'ELF CPU')
    require(struct.unpack_from('<I', data, 24)[0] == 0xc00d638, 'ELF entry')
    shoff = struct.unpack_from('<I', data, 32)[0]
    shsize, count, names_index = struct.unpack_from('<3H', data, 46)
    require(shsize == 40, 'ELF section header size')
    rows = [struct.unpack_from('<10I', data, shoff+i*40) for i in range(count)]
    strings = rows[names_index]
    names = data[strings[4]:strings[4]+strings[5]]
    output = {}
    for row in rows:
        name = names[row[0]:].split(b'\0', 1)[0].decode('ascii')
        if row[2] & 2 and row[5]:
            require(name in SLOTS and row[1] == 1 and row[2] == 6,
                    f'Unexpected allocated ELF section {name}')
            address, capacity, _ = SLOTS[name]
            require(row[3] == address and row[5] <= capacity, f'Section extent {name}')
            output[name] = data[row[4]:row[4]+row[5]]
    require(set(output) == set(SLOTS), 'Both hooks required')
    return output

def main(profile='stripes'):
    require(profile in ('stripes', 'geometry'), 'Unsupported fixed probe profile')
    geometry = profile == 'geometry'
    stem = 'lcd_geometry' if geometry else 'lcd_probe'
    source = ROOT/'Atorch/T3P Firmware Version 3.0.3.bin'
    folder = ROOT/'local/unpacked'/source.name
    raw = source.read_bytes()
    plain = (folder/'decrypted.bin').read_bytes()
    app = (folder/'files/app.bin').read_bytes()
    require(digest(raw) == STOCK and digest(app) == APP, 'Only exact stock3.0.3 supported')
    require(app == plain[0x5100:0x5100+len(app)], 'Extraction differs')
    cfg = json.loads((folder/'jlfw.yaml').read_text(encoding='utf-8'))
    require(cfg['base-offset'] == 0 and cfg['entry-point'] == BASE, 'Stock layout')
    for name, (address, size, sha) in SLOTS.items():
        require(digest(read(app, address, size)) == sha, f'Stock function changed {name}')
    # Table layout and SPI-only specialization must match this exact image.
    require(struct.unpack('<2I', read(app, 0x11faf8, 8)) == (0xc02ec4e, 0xc02ec96),
            'Stock SPI draw/continue pointers')
    params = struct.unpack('<18I', read(app, 0x120164, 72))
    require(params[2:6] == (320,320,320,320) and params[6] == 0 and
            params[8] == 12800 and params[11] == 1 and params[16] == 1 and params[17] == 0x27,
            'Only this 320x320/SPI/RGB565/buffer layout supported')
    require(struct.unpack('<2I', read(app, 0xc0b6a58, 8)) == (0xc01105a,0xc01106a),
            'Stock LCD interface callbacks')
    code = list(instructions(ROOT/'analysis/app-3.0.3.disasm.txt'))
    external_interior = []
    for pc, opcode, text, _ in code:
        require(app[pc-BASE:pc-BASE+len(opcode)] == opcode, 'Stock disassembly bytes differ')
        match = re.search(r'\b(?:call|goto) (-?0x[0-9a-f]+)\b', text)
        if not match: continue
        target = pc + len(opcode) + int(match[1],16)
        for name, (start, size, _) in SLOTS.items():
            if start < target < start+size and not start <= pc < start+size:
                external_interior.append((hex(pc),hex(target),name))
    require(not external_interior, f'External branch into replaced function: {external_interior}')
    interior_pointers = []
    for off in range(0,len(app)-3,2):
        value = struct.unpack_from('<I',app,off)[0]
        if any(a < value < a+n for a,n,_ in SLOTS.values()):
            interior_pointers.append((hex(BASE+off),hex(value)))
    require(not interior_pointers, f'Interior address constants need review: {interior_pointers}')

    out = ROOT/('artifacts/lcd-geometry' if geometry else 'artifacts/lcd-probe')
    out.mkdir(parents=True,exist_ok=True)
    tools = ROOT/'local/tools/jieli/C$/JL/pi32/bin'
    obj, elf = out/(stem+'.o'), out/(stem+'.elf')
    commands = [
        [str(tools/'clang.exe'),'-target','pi32v2','-mcpu=r3','-integrated-as','-Oz',
         '-ffreestanding','-fno-builtin','-fno-stack-protector','-ffunction-sections',
         '-fdata-sections','-mllvm','-pi32v2-large-program=true','-c',
         str(ROOT/'native'/(stem+'.c')),'-o',str(obj)],
        [str(tools/'pi32v2-ld.exe'),'-T',str(ROOT/'native'/(stem+'.ld')),
         '-Map='+str(out/(stem+'.map')),'-o',str(elf),str(obj)],
    ]
    for command in commands:
        result = subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace')
        require(result.returncode == 0, result.stdout+result.stderr)
    result = subprocess.run([str(tools/'llvm-objdump.exe'),'-d',str(elf)],
                            capture_output=True,text=True,encoding='utf-8',errors='replace')
    require(result.returncode == 0, 'Probe disassembly failed')
    disasm = out/(stem+'.disasm.txt')
    disasm.write_text(result.stdout,encoding='utf-8')
    payloads = elf_sections(elf.read_bytes())
    calls = {name: [] for name in SLOTS}
    for pc, opcode, text, _ in instructions(disasm):
        require('call r' not in text, 'Unreviewed indirect call')
        match = re.search(r'\bcall (-?(?:0x[0-9a-f]+|[0-9]+))',text)
        if match:
            name = next(n for n,(a,_,_) in SLOTS.items() if a <= pc < a+len(payloads[n]))
            calls[name].append(pc+len(opcode)+int(match[1],0))
    expected_calls = EXPECTED_CALLS
    if geometry:
        # These internal helper addresses are pinned to this fixed build too.
        # Moving either helper requires reviewing its placement/callers again.
        expected_calls = {
            '.probe_draw': [0xc00d676,0xc006ec6,0xc0110b8,0xc02ec4e],
            '.probe_continue': [0xc00d676,0xc006ec6,0xc006efa,0xc0110b8,0xc02ec96],
        }
        require(len(payloads['.probe_draw']) == 142 and
                len(payloads['.probe_continue']) == 166, 'Geometry helper layout changed')
    require(calls == expected_calls, f'Unexpected direct calls: {calls}')
    patched = bytearray(app)
    ranges = []
    for name,payload in payloads.items():
        address,capacity,_ = SLOTS[name]
        offset = address-BASE
        patched[offset:offset+len(payload)] = payload
        ranges.append((offset,offset+len(payload)))
    changed = [i for i,(a,b) in enumerate(zip(app,patched)) if a != b]
    require(changed and all(any(a <= i < b for a,b in ranges) for i in changed), 'App diff escaped hooks')
    require(len(patched) == len(app), 'App size changed')
    encoded,modified,bounds = rebuild_in_memory(raw,plain,cfg['chip-key'],patched)
    # Independently decrypt every changed encrypted block and compare the full plaintext.
    decoded = bytearray(plain)
    changed_blocks = []
    for off in range(0x5000,len(raw),32):
        if encoded[off:off+32] == raw[off:off+32]: continue
        block = bytearray(encoded[off:off+32])
        jl_enc_cipher(block,0,32,cfg['chip-key']^((off-0x5000)>>2))
        decoded[off:off+32] = block
        changed_blocks.append(off)
    require(decoded == modified, 'Full changed-block plaintext differs')
    area, app_entry = entry(decoded,0x5000), entry(decoded,0x5020)
    require(binascii.crc_hqx(decoded[bounds[0]:bounds[1]],0) == app_entry['data_crc'], 'App CRC')
    require(binascii.crc_hqx(decoded[0x5020:0x5000+area['size']],0) == area['data_crc'], 'Area CRC')
    require(len(encoded) == len(raw) and encoded[:0x5000] == raw[:0x5000], 'Boot prefix/size changed')
    output = out/('T3P_LCD_GEOMETRY_3.0.3.bin' if geometry else 'T3P_LCD_PROBE_3.0.3.bin')
    output.write_bytes(encoded)
    report = dict(status='BUILT AND OFFLINE VERIFIED; NOT DEVICE TESTED',
        purpose=('Native RGB bars and240-coordinate border on stock startup; not DOOM' if geometry
                 else 'Native magenta/green partial-buffer drawing on stock startup; not DOOM'),
        source_sha256=digest((ROOT/'native'/(stem+'.c')).read_bytes()),
        linker_script_sha256=digest((ROOT/'native'/(stem+'.ld')).read_bytes()),
        compiler_sha256=digest((tools/'clang.exe').read_bytes()),
        linker_sha256=digest((tools/'pi32v2-ld.exe').read_bytes()),
        elf_sha256=digest(elf.read_bytes()),
        stock_sha256=STOCK,stock_app_sha256=APP,output=str(output.relative_to(ROOT)),
        sha256=digest(encoded),bytes=len(encoded),app_bytes=len(app),app_changed_bytes=len(changed),
        hooks=[dict(section=n,address=hex(SLOTS[n][0]),bytes=len(p),capacity=SLOTS[n][1],
                    original_function_sha256=SLOTS[n][2]) for n,p in payloads.items()],
        direct_calls={n:[hex(a) for a in v] for n,v in calls.items()},
        external_calls={n:[hex(a) for a in v if a in (0xc006ec6,0xc006efa,0xc02ec4e,0xc02ec96)]
                        for n,v in calls.items()},
        changed_cipher_blocks=len(changed_blocks),boot_prefix_20_KiB_identical=True,
        original_startup_and_USB_code_identical=True,app_directory_other_entries_identical=True,
        resource_and_tail_identical=True,all_four_nested_CRCs_valid=True,
        external_branches_to_hook_interiors=[],interior_address_constants=[],
        static_RAM_added_bytes=0,buffer_limit_bytes=12800,
        expected_pattern=('RGB80-column bars, white240x240 border/top12rows, yellow beyond240'
                          if geometry else 'Alternating 32-pixel vertical magenta/green bands, replacing stock UI pixels'),
        native_entry_and_RTOS='Unchanged stock3.0.3 startup; existing draw callback invokes compiled probe',
        device_access=False,device_flashed=False,device_tested=False,
        restore_file='artifacts/stock-restore-3.0.3/T3P Firmware Version 3.0.3.bin',
        caveats=['Physical RGB order, panel geometry and visibility remain untested',
                 'UI labels become unreadable while hooked; this is a drawing experiment',
                 'Full-flash backup and calibration preservation remain unverified',
                 'No guarantee of recovery after arbitrary boot-region corruption'])
    (out/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=True,indent=2))

if __name__ == '__main__': main()
