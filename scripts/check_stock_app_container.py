"""In-memory stock app replacement checks. Writes JSON only, never firmware.

The input is always one of the four verified STOCK applications. A one-byte
canary is changed in memory and immediately discarded. This is not an update
packer, has no payload argument and never opens a device.
"""
import binascii
import hashlib
import json
from pathlib import Path
import struct
import sys

from audit_stock_recovery import ROOT,entry,require
sys.path.insert(0,str(ROOT/'vendor/jl-misctools/firmware'))
from jltech.cipher import jl_enc_cipher

def rebuild_in_memory(original,plain,key,payload):
    start=0x5000
    area=entry(plain,start);app=entry(plain,start+32)
    require(area['name']=='app_area_head' and app['name']=='app.bin','Unexpected layout')
    require(len(payload)==app['size'],'App footprint must remain EXACTLY unchanged')
    app_start=start+app['offset'];app_end=app_start+app['size']
    require(app_start==0x5100 and app['size']%32==0,'App block alignment')
    require(app_end<=start+area['size']<=len(original),'App bounds')
    output=bytearray(original);changed_plain=bytearray(plain)
    changed_plain[app_start:app_end]=payload
    struct.pack_into('<H',changed_plain,start+34,binascii.crc_hqx(payload,0))
    struct.pack_into('<H',changed_plain,start+32,binascii.crc_hqx(changed_plain[start+34:start+64],0))
    struct.pack_into('<H',changed_plain,start+2,binascii.crc_hqx(changed_plain[start+32:start+area['size']],0))
    struct.pack_into('<H',changed_plain,start,binascii.crc_hqx(changed_plain[start+2:start+32],0))
    for lo,hi in ((start,start+64),(app_start,app_end)):
        for off in range(lo,hi,32):
            block=bytearray(changed_plain[off:off+32])
            jl_enc_cipher(block,0,len(block),key^((off-start)>>2))
            output[off:off+32]=block
    require(output[:start]==original[:start],'Protected boot prefix changed')
    require(output[start+64:app_start]==original[start+64:app_start],'Other app directory entries changed')
    require(output[app_end:]==original[app_end:],'Resource/tail changed')
    return bytes(output),changed_plain,(app_start,app_end)

def main():
    inventory=json.loads((ROOT/'analysis/stock-inventory.json').read_text(encoding='utf-8'))
    results=[]
    for row in inventory:
        if not row['file'].endswith('.bin'):continue
        source=ROOT/row['file'];raw=source.read_bytes()
        require(hashlib.sha256(raw).hexdigest()==row['sha256'],'Stock source changed')
        folder=ROOT/'local/unpacked'/source.name
        cfg=json.loads((folder/'jlfw.yaml').read_text(encoding='utf-8'))
        plain=(folder/'decrypted.bin').read_bytes();app=(folder/'files/app.bin').read_bytes()
        area=entry(plain,0x5000)
        require(binascii.crc_hqx(plain[0x5020:0x5000+area['size']],0)==area['data_crc'],'Original area CRC')
        encoded,_,bounds=rebuild_in_memory(raw,plain,cfg['chip-key'],app)
        require(encoded==raw,'No-op round trip differs from original encrypted file')
        canary=bytearray(app);canary[0]^=1
        encoded,modified,bounds=rebuild_in_memory(raw,plain,cfg['chip-key'],canary)
        require(encoded!=raw,'Canary had no effect')
        newarea=entry(modified,0x5000);newapp=entry(modified,0x5020)
        require(binascii.crc_hqx(canary,0)==newapp['data_crc'],'Canary app CRC')
        require(binascii.crc_hqx(modified[0x5020:0x5000+newarea['size']],0)==newarea['data_crc'],'Canary area CRC')
        # Decrypt changed ciphertext blocks independently and compare bytes.
        for off in (0x5000,0x5020,bounds[0]):
            block=bytearray(encoded[off:off+32]);jl_enc_cipher(block,0,32,cfg['chip-key']^((off-0x5000)>>2))
            require(block==modified[off:off+32],'Changed block cipher check')
        rejected=False
        try:rebuild_in_memory(raw,plain,cfg['chip-key'],app+b'\0')
        except ValueError:rejected=True
        require(rejected,'Oversized app was not rejected')
        results.append(dict(file=source.name,no_op_ciphertext_identical=True,
            one_byte_canary_in_memory_only=True,nested_header_and_payload_CRC_valid=True,
            changed_cipher_blocks_checked=True,boot_prefix_unchanged=True,
            VM_PRCT_BTIF_EXIF_entries_unchanged=True,resource_and_tail_unchanged=True,
            oversized_payload_rejected=True,app_budget_bytes=len(app),app_payload_range=list(bounds)))
    link=json.loads((ROOT/'artifacts/pi32v2-engine/linkcheck.json').read_text())
    require(hashlib.sha256((ROOT/'artifacts/pi32v2-engine/DO-NOT-FLASH-linkcheck.elf').read_bytes()).hexdigest()==link['elf_sha256'],'Linkcheck changed')
    smallest=min(x['app_budget_bytes'] for x in results)
    require(link['flash_total_bytes']<smallest,'Prototype exceeds smallest app')
    report=dict(status='OFFLINE IN-MEMORY CHECKS ONLY; NO FIRMWARE FILE GENERATED',stock_images=results,
        smallest_stock_app_budget=smallest,prototype_flash_span=link['flash_total_bytes'],
        bytes_left_before_real_startup_and_drivers=smallest-link['flash_total_bytes'],
        original_file_lengths_preserved=True,device_access=False,generated_update_image=False,
        warning='Existing linkcheck is not bootable and is NOT accepted as payload. Runtime boot base, installed layout and calibration preservation are not established by these checks.')
    (ROOT/'analysis/stock-app-container-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(stock_images=len(results),no_op_roundtrips=4,in_memory_canary_checks=4,
        oversized_payload_rejections=4,smallest_app_budget=smallest,
        remaining_before_startup_and_drivers=report['bytes_left_before_real_startup_and_drivers'],
        firmware_files_generated=0,device_access=False)))

if __name__=='__main__':main()
