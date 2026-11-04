"""Build native board integration offline; never open USB or launch an updater.

Default: package a standalone moving-bar bring-up test into the exact3.0.3
container. --doom links the actual engine/backend. --package-doom also makes
an update file after verifying hardware evidence for the shared backend.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from check_stock_app_container import rebuild_in_memory
from generate_lcd_sequence import generate
from runtime_layout_evidence import inspect_layout
from stock_data import ROOT,BASE
from jltech.cipher import jl_enc_cipher


def digest(data):return hashlib.sha256(data).hexdigest()


def flatten(elf):
    data=elf.read_bytes()
    offset=struct.unpack_from('<I',data,28)[0]
    stride,count=struct.unpack_from('<HH',data,42)
    loads=[]
    for p in range(offset,offset+stride*count,stride):
        kind,at,vaddr,physical,size,memsize,flags,align=struct.unpack_from('<8I',data,p)
        if kind==1 and size:loads.append((at,vaddr,physical,size))
    # The old linker may include ELF headers before .runtime_entry in its
    # first PT_LOAD. Export allocated PROGBITS sections only, using each
    # section's containing load segment to resolve .data's Flash load address.
    shoff=struct.unpack_from('<I',data,32)[0]
    shsize,shcount=struct.unpack_from('<HH',data,46)
    segments=[]
    for p in range(shoff,shoff+shsize*shcount,shsize):
        _,kind,flags,address,at,size,_,_,_,_=struct.unpack_from('<10I',data,p)
        if kind!=1 or not flags&2 or not size:continue
        matches=[physical+at-start for start,virtual,physical,length in loads
                 if start<=at and at+size<=start+length and virtual+at-start==address]
        if len(matches)!=1:raise ValueError('Ambiguous/missing section load segment')
        physical=matches[0]
        if not BASE<=physical<physical+size<=BASE+0x165d40:
            raise ValueError('Unexpected section load address')
        segments.append((physical-BASE,data[at:at+size]))
    assert segments and min(a for a,_ in segments)==0
    segments.sort()
    for a,b in zip(segments,segments[1:]):assert a[0]+len(a[1])<=b[0]
    image=bytearray(b'\xff'*0x165d40)
    for at,blob in segments:image[at:at+len(blob)]=blob
    return bytes(image),max(a+len(b) for a,b in segments)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--doom',action='store_true')
    parser.add_argument('--package-doom',action='store_true')
    parser.add_argument('--toolchain',type=Path,default=ROOT/'local/tools/jieli/C$/JL/pi32')
    parser.add_argument('--game-data',type=Path,default=Path('D:/doom_on_km003c/vendor/dmc100-doom/build/packs/E1M1/DOOM.DAT'))
    parser.add_argument('--revision',type=int,default=2)
    args=parser.parse_args();generate()
    if args.package_doom:
        if not args.doom:parser.error('--package-doom requires --doom')
        baseline=ROOT/'analysis/native-bringup-v2-build-reference.json'
        if not baseline.is_file():baseline=ROOT/'artifacts/native-bringup-v2/manifest.json'
        proven=json.loads(baseline.read_text(encoding='utf-8'))
        evidence=json.loads((ROOT/proven['physical_evidence']).read_text(encoding='utf-8'))
        assert evidence['image_sha256']==proven['image_sha256'] and evidence['moving_marker_confirmed']
        changed_sources=[
            name for name,sha in proven['source_sha256'].items()
            if digest((ROOT/'native'/name).read_bytes())!=sha
        ]
        allowed_changes={'t3p_bsp.c','bsp_platform.c'} if args.revision in (3,4,5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27) else set()
        allowed_changes.update({"t3p_display.h","t3p_bsp.h"})  # DMA alignment is conditional.
        allowed_changes.add("t3p_display.c")
        assert set(changed_sources)<=allowed_changes,changed_sources
    assert args.revision>=2
    doom_dir='native-doom-integration' if args.revision==2 else f'native-doom-v{args.revision}'
    out=ROOT/'artifacts'/(doom_dir if args.doom else f'native-bringup-v{args.revision}')
    out.mkdir(exist_ok=True)
    toolchain=args.toolchain.resolve();tools=toolchain/'bin'
    names=['runtime_start.S','runtime_memory.c','t3p_signal.c','t3p_boot.c','t3p_bsp.c','t3p_lcd_sequence.c',
           't3p_bsp_native.c','bsp_platform.c']
    if not args.doom or args.revision in (17,18,19,20,21,22,23,24,25,26,27):names+=['t3p_display.c']
    small_pages=args.doom and args.revision in (6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27)
    expanded_cache=args.doom and args.revision==11
    cache_pages=60 if expanded_cache else 42 if args.revision in (14,15,16,17,18,19,20,21,22,23,24,25,26,27) else 48
    if args.doom and args.revision in (21,22,23,24,25,26,27):cache_pages=35
    hot_raw_pages=(0x349,0x547,0x007,0x022) if args.revision in (10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27) else ()
    if args.doom and args.revision in (5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27):names+=['t3p_game.c']
    if args.doom and args.revision in (12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27):names+=['../vendor/tinf/tinflate.c']
    if args.revision in (16,17,18,19,20,21,22,23,24,25,26,27):
        hot_raw_pages+=(0x404,0x34e,0x36e,0x440)
    if args.revision in (20,21,22,23,24,25,26,27):
        hot_raw_pages+=(0x353,0x34f,0x00d,0x441,0x442,0x3b4)
    if args.revision in (22,23,24,25,26,27):
        hot_raw_pages+=(0x555,0x3fd,0x005)
    if args.revision in (23,24,25,26,27):
        hot_raw_pages+=(0x229,0x534)
    if args.revision in (25,26,27):
        hot_raw_pages+=(0x3f8,0x435,0x00e,0x549,0x3fc,0x405,0x539)
    asset_flags=[]
    if args.doom and args.revision in (14,15,16,17,18,19,20,21,22,23,24,25,26,27):
        raw=args.game_data.read_bytes()
        for pos in range(256,512,16):
            kind, count, start, size, _=struct.unpack_from('<HHIII',raw,pos)
            if kind in (3,4,6):
                end=start+(count*1024 if kind==4 else size)
                assert start<end<=len(raw)
                asset_flags.extend([f'-DT3P_ASSET_{kind}_START={start}u',f'-DT3P_ASSET_{kind}_END={end}u'])
        assert len(asset_flags)==6
    if args.doom and args.revision in (21,22,23,24,25,26,27):names+=["doom_bridge.c"]
    objects=[]
    for name in names:
        obj=out/(Path(name).name+'.o')
        flags=['-target','pi32v2','-mcpu=r3','-integrated-as','-O2','-ffreestanding',
               '-fno-builtin','-fno-stack-protector','-ffunction-sections','-fdata-sections',
               '-mllvm','-pi32v2-large-program=true','-Wno-macro-redefined',
               '-Wno-unused-command-line-argument','-I',str(ROOT/'vendor/dmc100-doom/engine/stub/include'),
               '-I',str(toolchain/'include/compiler'),'-I',str(toolchain/'pi32v2-include')]
        if name.endswith('.S'):flags+=['-x','assembler']
        if not args.doom:flags+=['-DT3P_BOARD_BRINGUP']
        if args.doom and args.revision in (4,5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27):flags+=['-DT3P_PROFILE']
        if args.doom and args.revision in (5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27):
            flags+=['-DT3P_PROFILE_ASSETS','-I',str(ROOT/'vendor/tinf')]
            flags+=['-DT3P_GAME_PAGE=2048',f'-DT3P_GAME_SLOTS={cache_pages}'] if small_pages else ['-DT3P_GAME_SLOTS=24']
        if args.doom and args.revision in (7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27):flags+=['-DT3P_PROFILE_HOT']
        if args.doom and args.revision in (9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27):flags+=['-DT3P_PROFILE_CACHE']
        if args.doom and args.revision in (13,14,15,16,17,18,19,20,21,22,23,24,25,26,27):flags+=['-DT3P_PROFILE_STAGES']
        if args.doom and args.revision in (15,16,17,18,19,20,21,22,23,24,25,26,27):flags+=["-DT3P_PROFILE_RAW_RANK"]
        if args.doom and args.revision in (17,18,19,20,21,22,23,24,25,26,27):flags+=["-DT3P_DBI_DMA"]
        if args.doom and args.revision in (18,19,20,21,22,23,24,25,26,27):flags+=["-DT3P_DBI_ACTIVE_LINE"]
        if args.doom and args.revision in (19,20,21,22,23,24,25,26,27):flags+=["-DT3P_DBI_ASYNC"]
        if args.doom and args.revision in (21,22,23,24,25,26,27):flags+=["-DT3P_INDEXED_FRAME"]
        if name=="doom_bridge.c":
            flags+=["-std=gnu11","-fno-common","-fwrapv","-fsigned-char","-DDMC_FW","-DPLAT_HOST",
                    "-DDMC_SINE_L1","-DKM_CACHE_KIB=4","-DKM_INPUT_AT_FRAME","-DKM_SQUARE_VIEW"]
            for folder in ("engine","port/data","port/render","port/obj","port/math","platform",
                           "port/game","port/ovl","gen"):
                flags+=["-I",str(ROOT/"vendor/dmc100-doom"/folder)]
        if args.doom and args.revision in (23,24,25,26,27):flags+=["-DT3P_PROFILE_PRESENT"]
        if args.doom and args.revision in (24,25,26,27):flags+=["-DT3P_DBI_CLOCK_DIV=2"]
        flags+=asset_flags
        if name.endswith('/tinflate.c') and args.revision==27:flags+=["-DT3P_TINF_PREFIX"]
        if name.endswith('/tinflate.c') and args.revision==26:flags+=["-DT3P_TINF_MATCH_COPY"]
        if name.endswith('/tinflate.c'):flags+=['-DNDEBUG','-DT3P_TINF_LOCAL_BITS']
        subprocess.run([str(tools/'clang.exe'),*flags,'-c',str(ROOT/'native'/name),'-o',str(obj)],check=True)
        objects.append(str(obj))
    game=out/'game.s';gameobj=out/'game.o'
    content='.section .game,"a",@progbits\n.balign 4\n.global t3p_game_image\nt3p_game_image:\n'
    if args.doom:
        asset=ROOT/'artifacts/game/E1M1.t3pg'
        if small_pages:
            from pack_game import pack,verify
            raw=args.game_data.read_bytes()
            assert hashlib.sha256(raw).digest()==asset.read_bytes()[32:64]
            image=pack(raw,2048,hot_raw_pages,try_strategies=args.revision in (16,17,18,19,20,21,22,23,24,25,26,27))
            verify(image,raw,2048)
            asset=out/'E1M1-2k.t3pg'
            asset.write_bytes(image)
        content+='.incbin '+json.dumps(asset.as_posix())+'\n'
    else:content+='.byte 0,0,0,0\n'
    content+='.global t3p_game_image_end\nt3p_game_image_end:\n'
    game.write_text(content,encoding='ascii')
    subprocess.run([str(tools/'clang.exe'),'-target','pi32v2','-mcpu=r3','-integrated-as',
                    '-c',str(game),'-o',str(gameobj)],check=True)
    elf=out/('DO-NOT-FLASH-doom-integration.elf' if args.doom else 'native-bringup.elf')
    libs=toolchain/'pi32v2-lib/r3-large'
    linker_script=ROOT/'native/runtime_layout.ld'
    driver_flash_reserve=98304
    driver_ram_reserve=8192 if expanded_cache else 32768
    if small_pages:
        driver_flash_reserve=81920
        layout_text=linker_script.read_text(encoding='utf-8')
        assert layout_text.count('. += 0x18000;')==1
        linker_script=out/'runtime_layout.ld'
        layout_text=layout_text.replace('. += 0x18000;','. += 0x14000;')
        if expanded_cache:
            assert layout_text.count('. += 0x8000;')==1
            layout_text=layout_text.replace('. += 0x8000;','. += 0x2000;')
        linker_script.write_text(layout_text,encoding='utf-8')
    cmd=[str(tools/'pi32v2-ld.exe'),'-T',str(linker_script),
         '--gc-sections','-Map='+str(out/'native.map'),'-o',str(elf),*objects,str(gameobj)]
    if args.doom:cmd+=[str(ROOT/'artifacts/pi32v2-engine/libt3pdoom.a')]
    cmd+=['--start-group',str(libs/'libcompiler-rt.a'),str(libs/'libm.a'),str(libs/'libc.a'),'--end-group']
    result=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',errors='replace')
    (out/'link.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    layout=inspect_layout(elf,driver_flash_reserve,driver_ram_reserve)
    with (out/'native.disasm.txt').open('w',encoding='utf-8') as f:
        subprocess.run([str(tools/'llvm-objdump.exe'),'-d','-print-imm-hex',str(elf)],stdout=f,check=True)
    payload,span=flatten(elf)
    (out/'DO-NOT-FLASH-app-payload.bin').write_bytes(payload)
    report=dict(status='PREPARED; native board execution has NOT been tested',
        purpose='Actual DOOM engine linked to real MMIO backend' if args.doom else 'Standalone LCD/clock/memory bring-up; no DOOM',
        elf_sha256=digest(elf.read_bytes()),payload_sha256=digest(payload),
        app_bytes=len(payload),load_span_bytes=span,layout=layout,
        initialized_hardware=['WDT disable','polling TIMER0 standard24MHz/4096',
                              'DBI divider selector 2; frequency unverified' if args.revision in (24,25,26,27) else 'DBI nominal6MHz clock selector','PC0 reset','stock ST7789V sequence',
                              'synchronous DBI command operations','LCDPG static open drain'],
        CPU_PLL_and_SFC='Inherited from stock bootloader, not retuned',
        ROM_imports='No SDK maskrom address bindings',
        USB_APP_interface=False,device_access=False,device_flashed=False,device_tested=False,
        pending=['physical cold boot and clock/LCD validation','input integration beyond known PA11'],
        expected_display=('Actual DOOM renderer; hardware execution pending successful standalone bring-up'
                          if args.doom else 'RGB bars, white border/top strip, black horizontal middle strip, white block moving along bottom'),
        restore='artifacts/stock-restore-3.0.3/T3P Firmware Version 3.0.3.bin')
    report['source_sha256']={name:digest((ROOT/'native'/name).read_bytes())
                            for name in names+['runtime_layout.ld','t3p_boot.h','t3p_signal.h','t3p_bsp.h','t3p_display.h']}
    report['compiler_sha256']=digest((tools/'clang.exe').read_bytes())
    if small_pages:
        report.update(game_page_bytes=2048,game_cache_pages=cache_pages,
                      game_forced_raw_pages=list(hot_raw_pages),
                      game_sha256=digest(asset.read_bytes()),
                      linker_script_sha256=digest(linker_script.read_bytes()),
                      driver_flash_reserve_bytes=driver_flash_reserve)
    if not args.doom or args.package_doom:
        source=ROOT/'Atorch/T3P Firmware Version 3.0.3.bin'
        raw=source.read_bytes();assert digest(raw)=='72571d747e9da86194deddae3802992f846415c45b82d7b28f8aa567af30acb2'
        extracted=ROOT/'local/unpacked'/source.name
        plain=(extracted/'decrypted.bin').read_bytes()
        cfg=json.loads((extracted/'jlfw.yaml').read_text(encoding='utf-8'))
        encoded,modified,bounds=rebuild_in_memory(raw,plain,cfg['chip-key'],payload)
        decoded=bytearray(plain)
        for off in range(0x5000,len(raw),32):
            if encoded[off:off+32]==raw[off:off+32]:continue
            assert off<0x5040 or bounds[0]<=off<bounds[1]
            block=bytearray(encoded[off:off+32]);jl_enc_cipher(block,0,len(block),cfg['chip-key']^((off-0x5000)>>2))
            decoded[off:off+32]=block
        assert decoded==modified
        assert encoded[:0x5000]==raw[:0x5000]
        assert encoded[bounds[1]:]==raw[bounds[1]:]
        doom_name='T3P_NATIVE_DOOM_E1M1_3.0.3.bin' if args.revision==2 else f'T3P_NATIVE_DOOM_E1M1_V{args.revision}_3.0.3.bin'
        firmware=out/(doom_name if args.doom else f'T3P_NATIVE_BRINGUP_V{args.revision}_3.0.3.bin');firmware.write_bytes(encoded)
        report.update(image_sha256=digest(encoded),image_bytes=len(encoded),
                      image_path=firmware.relative_to(ROOT).as_posix(),
                      boot_prefix_20_KiB_identical=True,other_entries_and_resource_tail_identical=True,
                      full_redecrypt_matches=True,all_four_nested_CRCs_rebuilt=True)
        if args.doom:
            report.update(hardware_backend_evidence=proven['physical_evidence'],
                          changed_sources_since_hardware_test=changed_sources,
                          doom_execution_verified=False)
        else:report.update(revision=args.revision,
                      changes=['Stock-matched LCD pin DIE=1 and DIR=0',
                               'Watchdog stopped before boot snapshot',
                               'Timer-independent entry signal and repeating error blink count'],
                      error_blink_codes={'1':'boot handoff validation','2':'timer start',
                                         '3':'LCD initialization or reset delay','4':'drawing or animation'})
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
