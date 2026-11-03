"""Offline T3P stock recovery audit. No USB, serial, loader or flash commands.

Source-byte checks establish stock control flow. Range calculations are a
conditional model for boot flash base0 and the supplied4KiB alignment, not a
readback of this unit. In particular the base0x10000 migration is NOT certified.
"""
import binascii
import hashlib
import itertools
import json
from pathlib import Path
import re
import struct
import sys

from find_stock_calls import instructions

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'vendor/jl-misctools/firmware'))
from jltech.cipher import jl_enc_cipher

BOOT_SHA='53b7b7f6ec1711becb538625a5afe4b45cc3dba5fafee8bcb229bd12d0791ec1'
BASE=0x102600

def require(value,message):
    if not value: raise ValueError(message)

def entry(data,at):
    hcrc,dcrc,offset,size,flags,reserved,index,name=struct.unpack_from('<HHIIBBH16s',data,at)
    require(binascii.crc_hqx(data[at+2:at+32],0)==hcrc,f'Entry CRC at{at:#x}')
    return dict(name=name.split(b'\0',1)[0].decode('ascii'),offset=offset,size=size,
                flags=flags,reserved=reserved,index=index,data_crc=dcrc)

def main():
    paths=sorted((ROOT/'local/unpacked').glob('*/top/uboot.boot'))
    require(len(paths)==4,'Expected4 boot copies')
    require(all(hashlib.sha256(p.read_bytes()).hexdigest()==BOOT_SHA for p in paths),'Boot hash')
    boot=paths[0].read_bytes()
    code={a:(raw,text.strip()) for a,raw,text,_ in instructions(ROOT/'analysis/uboot.disasm.txt')}
    # Keep explicit addresses and operands; no inference from strings alone.
    checks={
        0x102600:'call 0x30ec',0x102626:'reti = 0x1037c0',
        0x1037f0:'r13 = 0x1068a0',
        0x103ea0:'r4 = r0 + -0x108',0x103ea4:'[r4+0xc] |= 0x800',
        0x103ea8:'[r4+0x8] |= 0x800',0x103eac:'[r4+0x14] |= 0x800',
        0x103ec0:'r5 = 0x100214',0x103ec6:'r2 = 0x8',
        0x103ece:'if (r0 == 0) goto 0xc',0x103ed0:'if (r9 != 0x0) goto 0xc',
        0x103ed4:'r0 = [r4+0x0]',0x103ed6:'if ((r0 & 0x800) == 0) goto 0x6',
        0x103eda:'goto 0x2ee',0x103edc:'d[r5+0x0] = r11_r10',
        0x104136:'if (r1 != 0x10000) goto 0x78',
        0x10416a:'call -0x12c6',0x104174:'[r0+0x4] = r8',
        0x104182:'call -0x12de',0x1041ac:'call -0x1254',
        0x1041b4:'r12 = r6 + 0x88',0x1041b8:'r5 = r13 + 0x6c',
        0x1041bc:'r2 = 0x0',0x1041c0:'r0 = r12',
        0x1041c4:'call -0x16c2',0x1041c8:'if (r0 == 0) goto 0xc4',
        0x10428e:'[r6+0x64] = r4',0x1042bc:'r0 = 0x105444',
        0x102bc4:'r0 = [r13+0x4]',0x102bca:'[r2+0x0] = r0',
        0x102bd2:'[r2+0x8] = r0',0x102e56:'r0 = b[r0+0x25] (u)',
        0x102e5a:'r0 = r0 << 0x8',0x102eca:'r14 = r2 + r0',
        0x102f1c:'r6 = 0x20',0x102f1e:'r5 = -0x1000',
        0x102f3a:'r0 = r5 & r14',0x104582:'call -0x1a80',
        0x1046be:'r1 = [r6+0x88]',0x1046c2:'r0 = r0 - r1',
        0x1046d4:'[r6+0x90] = r7',0x1046da:'call -0x1836',
        0x104710:'r0 = 0xbb',0x1047b0:'call -0x16f0',
        0x104888:'[r6+0x90] = r10',0x10488c:'r0 = r12',
        0x104896:'call -0x17d6',0x104c8e:'call r1',
    }
    evidence=[]
    for address,fragment in checks.items():
        raw,insn=code[address]
        require(fragment in insn,f'Instruction{address:#x}')
        require(boot[16+address-BASE:16+address-BASE+len(raw)]==raw,f'Bytes{address:#x}')
        evidence.append(dict(address=hex(address),raw_hex=raw.hex(),instruction=insn))
    text=(ROOT/'analysis/uboot.disasm.txt').read_text(encoding='utf-8-sig')
    block=text[text.index('  1046aa:'):text.index('  1046be:')]
    require('} else {' in block,'Do not flatten predicated if/else in transfer-length calculation')
    calls=[]
    for address,target in ((0x103eca,0xffd74),(0x1041c4,0x102b06),
                           (0x104582,0x102b06),(0x1046da,0x102ea8),
                           (0x1047b0,0x1030c4),(0x104896,0x1030c4),
                           (0x10416a,0x102ea8),(0x104182,0x102ea8),
                           (0x1041ac,0x102f5c)):
        raw,insn=code[address];displacement=int(re.search(r'call (-?0x[0-9a-f]+)',insn)[1],16)
        actual=address+len(raw)+displacement
        require(actual==target,f'Call{address:#x} actual{actual:#x} expected{target:#x}')
        calls.append(dict(address=hex(address),target=hex(target)))
    def read_boot(a,n):return boot[16+a-BASE:16+a-BASE+n]
    require(read_boot(0x1068c8,8)==b'ubootZst','Update tag')
    require(read_boot(0x10690c,13)==b'app_dir_head\0','Opened app directory')
    boot_report=bytes.fromhex('0600ff0901a101090216000026ff00750895408102090316000026ff00750895409102c0')
    require(read_boot(0x106a3f,len(boot_report))==boot_report,'Boot HID report descriptor')
    require(boot.count(bytes.fromhex('120110010000004083045057000101020301'))==1,'Boot USB device descriptor')
    header=(ROOT/'local/jieli-sdk/SDK/interface/driver/cpu/br35/asm/br35.h').read_text(errors='replace')
    require('JL_PORTA_BASE' in header and 'map_adr(0x30, 0x00)' in header,'PORTA header')
    require('u32 IN;' in header and 'u32 DIR;' in header,'PORT fields')
    boot_h=(ROOT/'local/jieli-sdk/SDK/interface/system/boot.h').read_text(errors='replace')
    require('u8 align;' in boot_h and 'n * 256' in boot_h,'Alignment units')
    inventory=json.loads((ROOT/'analysis/stock-inventory.json').read_text(encoding='utf-8'))
    models=[]
    for item in inventory:
        if not item['file'].endswith('.bin'):continue
        source=ROOT/item['file'];raw=source.read_bytes()
        require(hashlib.sha256(raw).hexdigest()==item['sha256'],'Stock image changed')
        require(hashlib.sha256(raw[:0x5000]).hexdigest()=='b0f4f136666588e03481a837cf3b2022dae35f5d3b5d2b78449affa2c15c6fe0','Shared boot prefix changed')
        folder=ROOT/'local/unpacked'/source.name
        cfg=json.loads((folder/'jlfw.yaml').read_text(encoding='utf-8'));require(cfg['base-offset']==0,'Source base')
        h=bytearray(raw[:32]);jl_enc_cipher(h,0,32,0xffff)
        require(binascii.crc_hqx(h[2:],0)==int.from_bytes(h[:2],'little'),'Flash header CRC')
        align=h[13]*256;require(align==4096,'Only supplied4KiB alignment modeled')
        decoded=(folder/'decrypted.bin').read_bytes()
        top=[entry(decoded,a) for a in range(0x20,0xc0,0x20)]
        appdir=next(x for x in top if x['name']=='app_dir_head');start=appdir['offset']
        require(start==0x5000 and appdir['flags']==0x81,'App directory')
        boot_entry=top[0];require(boot_entry['name']=='uboot.boot','Boot entry')
        require(boot_entry['offset']+boot_entry['size']<=start,'Boot before app')
        size=len(raw);require(size%align==0,'Only aligned source images modeled')
        require(size>start and size<=0x1ff000,'Image outside key_mac')
        pages=list(range(start,size,align))
        require(pages and all(p>=start and p+align<=size for p in pages),'Erase bounds')
        # PC start code uses ceil(file_size/58). The receiver's byte4 length
        # interpretation differs from the observed AA06 PC header (byte4 is
        # its CRC). Do NOT generate protocol packets from this model. For
        # aligned STOCK sizes the erase-page result is nevertheless invariant
        # for every possible byte4, including0. Check the actual ceil256 then
        # integer division by erase_unit/256 sequence, not an idealized ceil4K.
        packet_count=(size+57)//58
        for byte4 in range(256):
            modeled_total=packet_count*58 if byte4==0 else (packet_count-1)*58+byte4
            erase_count=((modeled_total-start+255)>>8)//(align>>8)
            require(erase_count==len(pages),'Header byte4 changes erase boundary')
        ents=[entry(decoded,a) for a in range(start+32,start+0xc0,32)]
        vm=next(x for x in ents if x['name']=='VM')
        # Reserved-entry absolute vs app-relative addressing is not resolved.
        # Keep BOTH possibilities in the report and compare conservatively.
        vm_candidates=[vm['offset'],start+vm['offset']]
        require(min(vm_candidates)>=size,'Stock update overlaps its own candidate VM start')
        models.append(dict(file=source.name,source_sha256=item['sha256'],source_bytes=size,
            boot_payload_end=boot_entry['offset']+boot_entry['size'],app_begin=start,
            erase_unit=align,normal_base0_erase_interval=[start,size],erase_sectors=len(pages),
            header_byte4_variants_checked=256,
            protected_boot_prefix=[0,start],normal_base0_boot_prefix_overlap=False,
            key_mac_begin=0x1ff000,normal_base0_key_mac_overlap=False,
            VM_offset_field=vm['offset'],VM_possible_physical_starts=vm_candidates,
            own_VM_candidates_overlap=False,
            app_bin_size=next(x for x in ents if x['name']=='app.bin')['size']))
    require(len(models)==4,'Expected4 models')
    crosses=[]
    for old,new in itertools.product(models,repeat=2):
        if old is new:continue
        earliest=min(old['VM_possible_physical_starts'])
        if new['source_bytes']>earliest:
            crosses.append(dict(installed_candidate=old['file'],update_candidate=new['file'],
                                possible_old_VM_overlap=[earliest,new['source_bytes']]))
    result=dict(status='OFFLINE CONDITIONAL EVIDENCE; NO DEVICE ACCESS; NOT FLASH AUTHORIZATION',
        boot_sha256=BOOT_SHA,identical_boot_copies=4,instruction_evidence=evidence,call_targets=calls,
        identical_encrypted_boot_prefix_bytes=0x5000,
        encrypted_boot_prefix_sha256='b0f4f136666588e03481a837cf3b2022dae35f5d3b5d2b78449affa2c15c6fe0',
        button_entry=dict(register='JL_PORTA.IN',address='0xfd3000',bit=11,active='low',
            app_call_not_required=True,physical_left_button_mapping='Firsthand external report only; no physical test'),
        boot_HID_profile=dict(vid='0483',pid='5750',usage_page='ff00',usage='0001',
                             report_descriptor_address='0x106a3f',report_descriptor_hex=boot_report.hex()),
        application_HID_profile=dict(vid='0483',pid='5750',usage_page='ff02',usage='0002'),
        entry_conditions=['ubootZst tag','r9 validation failure flag','PA11 low'],
        normal_app_path='0x1041ca onward; application indirect call0x104c8e',
        update_path='0x103edc/0x103ee0 -> USB/HID setup -> receive loop',
        important_exception='Runtime boot flash base0x10000 selects pre-USB erase/copy migration; no unconditional no-write boot claim',
        range_model_assumptions=['runtime flash base0','root directory matches supplied image','erase unit4096','stock aligned transfer length','no malformed/oversized header','app_dir_head remains root entry'],
        images=models,possible_cross_version_VM_overlap=crosses,
        findings=['Stock bootloader contains its own button gate before application execution',
            'Normal modeled updates avoid boot prefix0..0x5000 and key_mac tail',
            'A larger update can overlap an older VM region; boot preservation alone does not preserve calibration/settings'],
        missing=['Actual unit boot flash base and stored top directory','Exact installed3.0.2 variant/layout',
                 'Mapping of individual calibration/settings into VM/EXIF/other regions',
                 'Exact PC/device transfer length convention; AA06 byte4 is CRC, so no updater sender is generated',
                 'Verified native startup and display image; current linkcheck ELF is not bootable'],
        next='Build a no-device app replacement size/layout checker; preserve smallest stock footprint and all boot bytes. No live update or manual wiring test yet.')
    out=ROOT/'analysis/stock-recovery-audit.json';out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(checked_instructions=len(evidence),checked_calls=len(calls),stock_images=4,
        conditional_boot_prefix_preserved=True,cross_version_VM_overlap_cases=len(crosses),
        device_access=False,live_flash_ready=False)))

if __name__=='__main__':main()
