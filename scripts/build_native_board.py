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
    parser.add_argument('--game-data',type=Path)
    parser.add_argument('--revision',type=int,default=2)
    parser.add_argument('--lz4-module-path',type=Path,default=ROOT/'local/host-lz4')
    args=parser.parse_args();generate()
    weapon_view_y=args.doom and args.revision in (50,51,52,53,54,55,56,57,58)
    touch_move=args.doom and args.revision in (51,52,53,54,55,56,57,58)
    dual_keys=args.doom and args.revision in (52,53,54,55,56,57,58)
    sensor_controls=args.doom and args.revision in (53,54,55,56,57,58)
    haptics=args.doom and args.revision in (55,56,57,58)
    uac_monitor=args.doom and args.revision in (56,57,58)
    seamless_view=args.doom and args.revision in (57,58)
    startup_banner=args.doom and args.revision==58
    # V50 through V58 inherit the V49 play configuration.
    revision=49 if weapon_view_y else args.revision
    lowres_game=args.doom and revision in (45,46,47,48,49)
    raw_game_only=lowres_game and revision in (46,47,48,49)
    if args.game_data is None:
        args.game_data=(ROOT/'artifacts/game-lowres/DOOM.DAT' if lowres_game else
                        Path('D:/doom_on_km003c/vendor/dmc100-doom/build/packs/E1M1/DOOM.DAT'))
    if lowres_game:
        lowres_manifest=json.loads((ROOT/'artifacts/game-lowres/manifest.json').read_text(encoding='utf-8'))
        assert digest(args.game_data.read_bytes())==lowres_manifest['raw_sha256']
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
        allowed_changes={'t3p_bsp.c','bsp_platform.c'} if revision in (3,4,5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49) else set()
        allowed_changes.update({"t3p_display.h","t3p_bsp.h"})  # DMA alignment is conditional.
        allowed_changes.add("t3p_display.c")
        # V40 adds a conditional publish barrier after copying SRAM code;
        # the V39 build compiles that branch out.
        if revision in (39,40,41,42,43,44,45,46,47,48,49):allowed_changes.add('runtime_memory.c')
        assert set(changed_sources)<=allowed_changes,changed_sources
    assert revision>=2
    doom_dir='native-doom-integration' if revision==2 else f'native-doom-v{args.revision}'
    out=ROOT/'artifacts'/(doom_dir if args.doom else f'native-bringup-v{revision}')
    out.mkdir(exist_ok=True)
    toolchain=args.toolchain.resolve();tools=toolchain/'bin'
    names=['runtime_start.S','runtime_memory.c','t3p_signal.c','t3p_boot.c','t3p_bsp.c','t3p_lcd_sequence.c',
           't3p_bsp_native.c','bsp_platform.c']
    if not args.doom or revision in (17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):names+=['t3p_display.c']
    small_pages=args.doom and revision in (6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49)
    expanded_cache=args.doom and revision==11
    cache_pages=60 if expanded_cache else 42 if revision in (14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49) else 48
    if args.doom and revision in (21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):cache_pages=35
    hot_raw_pages=(0x349,0x547,0x007,0x022) if revision in (10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49) else ()
    if args.doom and revision in (5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):names+=['t3p_game.c']
    if args.doom and revision in (12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):names+=['../vendor/tinf/tinflate.c']
    if revision in (16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):
        hot_raw_pages+=(0x404,0x34e,0x36e,0x440)
    if revision in (20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):
        hot_raw_pages+=(0x353,0x34f,0x00d,0x441,0x442,0x3b4)
    if revision in (22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):
        hot_raw_pages+=(0x555,0x3fd,0x005)
    if revision in (23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):
        hot_raw_pages+=(0x229,0x534)
    if revision in (25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):
        hot_raw_pages+=(0x3f8,0x435,0x00e,0x549,0x3fc,0x405,0x539)
    lz4_pages=()
    if args.doom and revision in (32,34,36,38):
        # Three raw donor pages fund four frequently decoded DEFLATE pages.
        lz4_pages=(0x349,0x007,0x547,0x535,0x537,0x538,0x544)
        import sys
        sys.path.insert(0,str(args.lz4_module_path.resolve()))
        names+=['t3p_lz4.c']
    if lowres_game:
        hot_raw_pages=tuple(range((args.game_data.stat().st_size+2047)//2048)) if raw_game_only else ()
        if raw_game_only:
            cache_pages=0
            names.remove('../vendor/tinf/tinflate.c')
    asset_flags=[]
    if args.doom and revision in (14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):
        raw=args.game_data.read_bytes()
        for pos in range(256,512,16):
            kind, count, start, size, _=struct.unpack_from('<HHIII',raw,pos)
            if kind in (3,4,6):
                end=start+(count*1024 if kind==4 else size)
                assert start<end<=len(raw)
                asset_flags.extend([f'-DT3P_ASSET_{kind}_START={start}u',f'-DT3P_ASSET_{kind}_END={end}u'])
        assert len(asset_flags)==6
    if args.doom and revision in (21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):names+=["doom_bridge.c"]
    if args.doom and revision in (41,42):names+=['t3p_tinf_profile.c']
    if args.doom and revision in (47,48,49):names+=['t3p_present.c']
    weapon_source='../vendor/dmc100-doom/engine/r_things.c'
    weapon_actions='../vendor/dmc100-doom/engine/p_pspr.c'
    menu_source='../vendor/dmc100-doom/engine/m_menu.c'
    if weapon_view_y:names+=[weapon_source]
    if touch_move:names+=['t3p_touch.c','t3p_sensors.c']
    if dual_keys:names+=['t3p_input.c']
    if sensor_controls:names+=['t3p_tilt.c','t3p_mic.c']
    if haptics:names+=['t3p_haptics.c',weapon_actions]
    if uac_monitor:names+=['t3p_baro.c',menu_source]
    band_source='../vendor/dmc100-doom/port/render/r_band.c'
    if seamless_view:names+=[band_source]
    objects=[]
    compile_defines={}
    for name in names:
        obj=out/(Path(name).name+'.o')
        flags=['-target','pi32v2','-mcpu=r3','-integrated-as','-O2','-ffreestanding',
               '-fno-builtin','-fno-stack-protector','-ffunction-sections','-fdata-sections',
               '-mllvm','-pi32v2-large-program=true','-Wno-macro-redefined',
               '-Wno-unused-command-line-argument','-I',str(ROOT/'vendor/dmc100-doom/engine/stub/include'),
               '-I',str(toolchain/'include/compiler'),'-I',str(toolchain/'pi32v2-include')]
        if name.endswith('.S'):flags+=['-x','assembler']
        if not args.doom:flags+=['-DT3P_BOARD_BRINGUP']
        if args.doom and revision in (4,5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=['-DT3P_PROFILE']
        if args.doom and revision in (5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):
            flags+=['-DT3P_PROFILE_ASSETS','-I',str(ROOT/'vendor/tinf')]
            flags+=['-DT3P_GAME_PAGE=2048',f'-DT3P_GAME_SLOTS={cache_pages}'] if small_pages else ['-DT3P_GAME_SLOTS=24']
        if args.doom and revision in (7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=['-DT3P_PROFILE_HOT']
        if args.doom and revision in (9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40):flags+=['-DT3P_PROFILE_CACHE']
        if args.doom and revision in (13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=['-DT3P_PROFILE_STAGES']
        if args.doom and revision in (15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40):flags+=["-DT3P_PROFILE_RAW_RANK"]
        if args.doom and revision in (17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=["-DT3P_DBI_DMA"]
        if args.doom and revision in (18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=["-DT3P_DBI_ACTIVE_LINE"]
        if args.doom and revision in (19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=["-DT3P_DBI_ASYNC"]
        if args.doom and revision in (21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=["-DT3P_INDEXED_FRAME"]
        if name in ("doom_bridge.c",weapon_source,weapon_actions,menu_source,band_source):
            flags+=["-std=gnu11","-fno-common","-fwrapv","-fsigned-char","-DDMC_FW","-DPLAT_HOST",
                    "-DDMC_SINE_L1","-DKM_CACHE_KIB=4","-DKM_INPUT_AT_FRAME","-DKM_SQUARE_VIEW"]
            for folder in ("engine","port/data","port/render","port/obj","port/math","platform",
                           "port/game","port/ovl","gen"):
                flags+=["-I",str(ROOT/"vendor/dmc100-doom"/folder)]
        if args.doom and revision in (23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=["-DT3P_PROFILE_PRESENT"]
        if args.doom and revision in (24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=["-DT3P_DBI_CLOCK_DIV=2"]
        if args.doom and revision in (28,29,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):
            flags+=["-DT3P_PROFILE_BENCH",f"-DT3P_BENCH_REVISION={revision}"]
        if args.doom and revision in (33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=["-DT3P_BENCH_FIXED_TICS"]
        if args.doom and revision in (32,34,36,38):flags+=["-DT3P_GAME_LZ4"]
        if args.doom and revision in (35,36):flags+=["-DT3P_PROFILE_BREAKDOWN"]
        if args.doom and revision in (37,38,39,40):
            capacity=(len(args.game_data.read_bytes())+2047)//2048
            flags+=['-DT3P_PROFILE_PAGE_TOTALS',f'-DT3P_PROFILE_PAGE_CAPACITY={capacity}']
        if args.doom and revision in (43,44,45,46,47,48,49):flags+=['-DT3P_BENCH_DECODE_CONTROL']
        if args.doom and revision in (41,42):
            flags+=['-DT3P_TINF_PROFILE','-I',str(ROOT/'native')]
            if revision==42:flags+=['-DT3P_TINF_PROFILE_SAMPLES']
        if lowres_game:
            if raw_game_only:flags+=['-DT3P_GAME_RAW_ONLY']
            else:flags+=['-DT3P_TINF_LITERAL_RUN']
        if args.doom and revision in (47,48,49):
            flags+=['-DT3P_FRAME_PRESENTER']
            if revision in (48,49):flags+=['-DT3P_PRESENT_OVERLAP']
        flags+=asset_flags
        if seamless_view and name in ('bsp_platform.c','t3p_present.c'):
            flags+=['-DT3P_SEAMLESS_VIEW','-I',str(ROOT/'vendor/dmc100-doom/platform')]
        if startup_banner and name in ('bsp_platform.c','t3p_present.c'):
            flags+=['-DT3P_STARTUP_BANNER']
        if name==weapon_source:flags+=['-DT3P_WEAPON_VIEW_Y']
        if touch_move and name=='bsp_platform.c':flags+=['-DT3P_TOUCH_MOVE']
        if dual_keys and name in ('bsp_platform.c','doom_bridge.c'):flags+=['-DT3P_DUAL_KEYS']
        if sensor_controls and name=='bsp_platform.c':
            flags+=['-DT3P_SENSOR_CONTROLS',f'-DT3P_SENSOR_REVISION={args.revision}']
        if haptics:flags+=['-DT3P_HAPTICS']
        if uac_monitor and name in ('bsp_platform.c',menu_source):
            flags+=['-DT3P_UAC_MONITOR','-I',str(ROOT/'native')]
        if uac_monitor and name in ('t3p_input.c','t3p_tilt.c'):
            flags+=['-DT3P_TILT_TUNING']
        if args.doom and revision==49:
            # Keep V48's proven transport/assets, restore the real game clock
            # and PA11 input; diagnostics must not borrow an active DMA engine.
            # The legacy HOT define also enables first-use CRC verification.
            # In a RAW_ONLY reader it allocates no hot-page counters.
            flags=[flag for flag in flags if flag=='-DT3P_PROFILE_HOT' or
                   not flag.startswith(('-DT3P_PROFILE','-DT3P_BENCH'))]
        if name.endswith('/tinflate.c') and revision in (27,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):flags+=["-DT3P_TINF_PREFIX"]
        if name.endswith('/tinflate.c') and revision in (39,40,41,42,43,44,45,46,47,48,49):flags+=['-DT3P_TINF_PREFIX_CONTINUE']
        if args.doom and revision in (39,40,41,42,43,44,45,46,47,48,49):flags+=['-DT3P_TINF_HOT_SECTION']
        if args.doom and revision==40:flags+=['-DT3P_TINF_RAM_HOT']
        if name.endswith('/tinflate.c') and revision==44:flags+=['-DT3P_TINF_LITERAL_RUN']
        if name.endswith('/tinflate.c') and revision==31:flags+=["-DT3P_TINF_PREFIX_BITS=8"]
        if name.endswith('/tinflate.c') and revision==26:flags+=["-DT3P_TINF_MATCH_COPY"]
        if name.endswith('/tinflate.c'):flags+=['-DNDEBUG','-DT3P_TINF_LOCAL_BITS']
        if args.doom and revision==49:
            compile_defines[name]=[flag[2:] for flag in flags if flag.startswith('-D')]
            assert not any(flag.startswith(('T3P_PROFILE','T3P_BENCH')) and flag!='T3P_PROFILE_HOT'
                           for flag in compile_defines[name])
        subprocess.run([str(tools/'clang.exe'),*flags,'-c',str(ROOT/'native'/name),'-o',str(obj)],check=True)
        objects.append(str(obj))
    game=out/'game.s';gameobj=out/'game.o'
    content='.section .game,"a",@progbits\n.balign 4\n.global t3p_game_image\nt3p_game_image:\n'
    if args.doom:
        asset=ROOT/'artifacts/game/E1M1.t3pg'
        if small_pages:
            from pack_game import pack,verify
            raw=args.game_data.read_bytes()
            if not lowres_game:assert hashlib.sha256(raw).digest()==asset.read_bytes()[32:64]
            image=pack(raw,2048,hot_raw_pages,try_strategies=revision in (16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49),lz4_pages=lz4_pages)
            verify(image,raw,2048)
            asset=out/'E1M1-2k.t3pg'
            asset.write_bytes(image)
        content+='.incbin '+json.dumps(asset.as_posix())+'\n'
    else:content+='.byte 0,0,0,0\n'
    content+='.global t3p_game_image_end\nt3p_game_image_end:\n'
    if startup_banner:
        banner_asset=ROOT/'assets/freedoom-banner.rgb565'
        banner_info=json.loads(banner_asset.with_suffix('.json').read_text(encoding='utf-8'))
        banner_bytes=banner_asset.read_bytes()
        if (len(banner_bytes)!=4+159*37*2 or struct.unpack_from('<HH',banner_bytes)!=(159,37)
                or digest(banner_bytes)!=banner_info['sha256']):
            raise ValueError('Invalid Freedoom startup banner')
        content+=('.section .rodata.t3p_startup_banner,"a",@progbits\n.balign 4\n'
                  '.global t3p_freedoom_banner\nt3p_freedoom_banner:\n'
                  '.incbin '+json.dumps(banner_asset.as_posix())+'\n')
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
        if args.doom and revision in (39,40,41,42,43,44,45,46,47,48,49):
            if revision!=40:
                assert layout_text.count('*(.rodata*)')==1
                layout_text=layout_text.replace('*(.rodata*)','*(.rodata*) *(.tinf_hot)')
            else:
                assert layout_text.count('        *(.data*)')==1
                layout_text=layout_text.replace('        *(.data*)','        *(.tinf_hot)\n        *(.data*)')
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
    layout=inspect_layout(elf,driver_flash_reserve,driver_ram_reserve,tinf_hot_sram=args.doom and revision==40)
    with (out/'native.disasm.txt').open('w',encoding='utf-8') as f:
        subprocess.run([str(tools/'llvm-objdump.exe'),'-d','-print-imm-hex',str(elf)],stdout=f,check=True)
    payload,span=flatten(elf)
    (out/'DO-NOT-FLASH-app-payload.bin').write_bytes(payload)
    report=dict(status='PREPARED; native board execution has NOT been tested',
        purpose='Actual DOOM engine linked to real MMIO backend' if args.doom else 'Standalone LCD/clock/memory bring-up; no DOOM',
        elf_sha256=digest(elf.read_bytes()),payload_sha256=digest(payload),
        app_bytes=len(payload),load_span_bytes=span,layout=layout,
        initialized_hardware=['WDT disable','polling TIMER0 standard24MHz/4096',
                              'DBI divider selector 2; frequency unverified' if revision in (24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49) else 'DBI nominal6MHz clock selector','PC0 reset','stock ST7789V sequence',
                              'synchronous DBI command operations','LCDPG static open drain'],
        CPU_PLL_and_SFC='Inherited from stock bootloader, not retuned',
        ROM_imports='No SDK maskrom address bindings',
        USB_APP_interface=False,device_access=False,device_flashed=False,device_tested=False,
        pending=['physical cold boot and clock/LCD validation','input integration beyond known PA11'],
        expected_display=('Actual DOOM renderer; hardware execution pending successful standalone bring-up'
                          if args.doom else 'RGB bars, white border/top strip, black horizontal middle strip, white block moving along bottom'),
        restore='artifacts/stock-restore-3.0.3/T3P Firmware Version 3.0.3.bin')
    report['source_sha256']={name:digest((ROOT/'native'/name).read_bytes())
                            for name in names+['runtime_layout.ld','t3p_boot.h','t3p_signal.h','t3p_bsp.h','t3p_display.h']+(['t3p_tinf_profile.h'] if args.doom and revision in (41,42) else [])+(['t3p_lz4.h'] if args.doom and revision in (32,34,36,38) else [])}
    if args.doom and revision in (47,48,49):
        report['source_sha256']['t3p_present.h']=digest((ROOT/'native/t3p_present.h').read_bytes())
    report['compiler_sha256']=digest((tools/'clang.exe').read_bytes())
    if small_pages:
        report.update(game_page_bytes=2048,game_cache_pages=cache_pages,
                      game_forced_raw_pages=list(hot_raw_pages),
                      game_sha256=digest(asset.read_bytes()),
                      linker_script_sha256=digest(linker_script.read_bytes()),
                      driver_flash_reserve_bytes=driver_flash_reserve)
    if args.doom and revision in (32,34,36,38):
        report.update(revision=revision,game_format_version=2,game_lz4_pages=list(lz4_pages),
                      game_forced_raw_pages=[p for p in hot_raw_pages if p not in lz4_pages],
                      game_pack_bytes=asset.stat().st_size,inflate_prefix_bits=7,
                      lz4_decoder='native/t3p_lz4.c; bounded raw blocks',
                      comparison_baseline_revision=30)
    if args.doom and revision in (33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49):
        report.update(revision=args.revision,comparison_baseline_revision=30,
                      game_pack_bytes=asset.stat().st_size,inflate_prefix_bits=7,
                      benchmark=dict(warmup_frames=10,measured_frames=30,input_enabled=False,
                                     freeze_when_complete=True,single_tic_per_frame=True,
                                     logical_game_clock='rendered frame counter / 35 Hz',
                                     measurement_clock='unchanged hardware km_millis',
                                     frame_hash='FNV-1a over 30 indexed frames; outside timed interval'),
                      profile_rows=['revision_hex','measured_frames','done_0xd00e',
                                    'frame_mean_ms','frame_min_ms','frame_max_ms',
                                    'decode_mean_ms','decode_min_ms','decode_max_ms',
                                    'present_mean_ms','present_min_ms','present_max_ms',
                                    'final_game_tic','indexed_frame_sequence_hash'])
    if args.doom and revision in (35,36):
        report['source_sha256']['t3p_game.h']=digest((ROOT/'native/t3p_game.h').read_bytes())
        report['benchmark']['instrumentation']='Nested millisecond timers; per-read overhead included; compare V35 with V36, not uninstrumented timings'
        report['comparison_baseline_revision']=35
        report['game_pack_identical_to_revision']=33 if revision==35 else 34
        report['profile_rows'] += [
            'compressed_fetch_mean_ms','raw_flash_copy_mean_ms','cached_page_copy_mean_ms',
            'first_use_crc_mean_ms','asset_read_inclusive_mean_ms','simulation_inclusive_mean_ms',
            'render_inclusive_mean_ms','present_call_mean_ms','frame_remainder_mean_ms',
            'deflate_mean_ms','lz4_mean_ms','raw_copy_total_bytes','compressed_fetch_total_bytes',
            'cached_page_copy_total_bytes','deflate_total_calls','lz4_total_calls']
        report['expected_display']='10 warmup + 30 measured frames; 30 diagnostic rows; input disabled; freezes at D00E'
    if args.doom and revision in (37,38,39,40):
        report['source_sha256']['t3p_game.h']=digest((ROOT/'native/t3p_game.h').read_bytes())
        report['comparison_baseline_revision']=37
        report['game_pack_identical_to_revision']=33 if revision==37 else 34
        report['benchmark']['page_ranking']='Top eight by total decode ms, then decode count, then page ID; generated after measured interval'
        report['benchmark']['page_capacity']=(len(args.game_data.read_bytes())+2047)//2048
        report['benchmark']['page_counters']='16-bit saturating time and count; reset before frame 11 without clearing warm caches or CRC state'
        report['benchmark']['instrumentation']='V35/V36 per-copy and phase probes disabled; existing staged decode timer retained'
        for rank in range(1,9):
            report['profile_rows'] += [f'rank_{rank}_page_hi16_total_ms_lo16',f'rank_{rank}_codec_hi16_count_lo16']
        report['expected_display']='10 warmup + 30 measured frames; 30 diagnostic rows; page ranks in rows 15-30; input disabled; freezes at D00E'
    if args.doom and revision in (39,40,41,42,43,44,45,46,47,48,49):
        report.update(comparison_baseline_revision=37,game_pack_identical_to_revision=37,
                      inflate_prefix_continuation=True,
                      decoder_change='Resume long Huffman codes at bit 8 after the 7-bit lookup; isolate the two noinline hot loops in a named Flash section shared with V40; same tree/table/stack sizes')
    if args.doom and revision==40:
        report.update(comparison_baseline_revision=39,
                      decoder_hot_loops_in_sram=True,
                      decoder_change='V39 prefix continuation plus tinf_decode_symbol and tinf_inflate_block_data executed from initialized SRAM; no cache/clock register changes')
    if args.doom and revision in (41,42):
        report.update(comparison_baseline_revision=41,game_pack_identical_to_revision=37,
                      decoder_hot_loops_in_sram=False,
                      decoder_change='V39 decoder with coarse DEFLATE phase timing and call counters; V42 also samples symbol and getbits_base helpers once in 64 calls')
        report['profile_rows'] += [
            'tree_preparation_ticks','compressed_payload_ticks','stored_block_ticks',
            'deflate_calls','dynamic_blocks','fixed_blocks','stored_blocks',
            'symbol_calls','symbol_sampled_ticks','symbol_samples',
            'getbits_base_calls','getbits_base_sampled_ticks','getbits_base_samples',
            'empty_probe_ticks','empty_probe_count','sample_shift']
        report['benchmark'].update(
            instrumentation='Raw 16-bit TIMER0 coarse phases; counters reset before frame 11, caches and CRC state retained; helper samples nested inside coarse phases',
            timer_hz_numerator=24000000,timer_hz_denominator=4096,
            tick_ms_numerator=64,tick_ms_denominator=375,
            helper_sample_period=64 if revision==42 else None,
            measurement_limits='Coarse ticks have about 170.7 us resolution; each timed span must be shorter than one 16-bit wrap (~11.18 s); deterministic 1/64 samples may alias; empty probes are descriptive, not an automatic correction')
        report['expected_display']='10 warmup + 30 measured frames; 30 rows; DEFLATE phases/counters in rows 15-30; input disabled; intentional D00E stop'
    if args.doom and revision in (43,44,45,46,47,48,49):
        report.update(comparison_baseline_revision=43,game_pack_identical_to_revision=37,
                      decoder_hot_loops_in_sram=False,inflate_literal_runs=revision==44,
                      decoder_change='V39 bounded decoder, without phase/helper or page-ranking instrumentation' if revision==43 else 'V43 plus bounded batches of prefix-table literals, keeping cursor and output local until fallback',
                      expected_display='10 warmup + 30 measured frames; 14 rows; input disabled; intentional D00E stop')
        report['benchmark']['instrumentation']='Existing outer frame/decode/present timers only; no tinf phase/helpers, page rank, cache-register counter reads or initial clock snapshot; compare V43 with V44'
    if lowres_game:
        expected=lowres_manifest['images']['E1M1-all-raw.t3pg' if raw_game_only else 'E1M1-deflate.t3pg']
        assert digest(asset.read_bytes())==expected['sha256'] and asset.stat().st_size==expected['bytes']
        report.pop('game_pack_identical_to_revision',None)
        report.update(comparison_baseline_revision=45,game_format_version=1,
                      game_raw_sha256=lowres_manifest['raw_sha256'],
                      game_data_profile=lowres_manifest['config_changes'],
                      game_raw_pages=expected['raw_pages'],game_deflate_pages=expected['deflate_pages'],
                      raw_game_only=raw_game_only,inflate_literal_runs=not raw_game_only,
                      inflate_prefix_continuation=not raw_game_only,inflate_prefix_bits=None if raw_game_only else 7,
                      decoder_change='No decoder or page/staging buffers; first-use CRC checked direct Flash reads' if raw_game_only else 'V44 decoder with the low-resolution asset set; no forced raw donor pages',
                      expected_display='10 warmup + 30 measured frames; 14 rows; input disabled; intentional D00E stop',
                      pending=['physical boot, low-resolution image quality and V45/V46 benchmark comparison',
                               'normal-play stability and input integration beyond known PA11'])
        report['source_sha256']['t3p_game.h']=digest((ROOT/'native/t3p_game.h').read_bytes())
        report['benchmark']['instrumentation']='Same outer frame/decode/present timers and 1 tic per frame; compare V45 compressed with V46 all-raw using identical raw assets. V44 has different image detail.'
        if raw_game_only:
            report['game_forced_raw_pages']='all'
            assert 'tinf_uncompress' not in (out/'native.map').read_text(encoding='utf-8')
    if args.doom and revision in (47,48,49):
        report.update(comparison_baseline_revision=47,game_pack_identical_to_revision=46,
                      expected_benchmark_final_tic=40,expected_benchmark_frame_hash='EBC8D404',
                      pending=['physical boot and V47/V48 throughput comparison',
                               'normal-play stability and input integration beyond known PA11'])
        report['benchmark'].update(
            frame_hash='FNV-1a over 30 indexed frames; inside timed interval',
            instrumentation='No intermediate LCD diagnostics; warmup drained before measured frame 11; final presentation drained inside measured frame 40; present rows measure commit wait/setup call plus final drain. Use V47 vs V48, not V46 timing.',
            image_sequence='Same 40 fixed tics and indexed image sequence as V46')
        report['profile_rows'][9:12]=['present_call_mean_ms','present_call_min_ms','present_call_max_ms']
        report['frame_presenter']=dict(
            indexed_buffers=2,indexed_side=120,retains_partial_HUD=True,
            palette_snapshot_bytes=512,rgb565_band_buffers=2,band_columns=16,
            band_height=240,overlap_render_and_previous_present=revision in (48,49),
            dma_ready_poll='cf_task_done, nonblocking, same existing timeout and blocking drain',
            service_hooks=['indexed column','RGB column','raw asset read','present call without new drawing'],
            interrupts=False,CPU_PLL_SFC_and_DBI_divider='unchanged from V46')
    if args.doom and revision==49:
        report.pop('benchmark',None)
        report.pop('expected_benchmark_final_tic',None)
        report.pop('expected_benchmark_frame_hash',None)
        report.update(comparison_baseline_revision=48,profile_rows=[],native_compile_defines=compile_defines,
                      expected_display='Continuous normal-clock DOOM; left/boot button fires; no diagnostic overlay or intentional frame-count stop',
                      play_mode=dict(clock='hardware km_millis; normal engine tic scheduling',
                                     fixed_tic_benchmark=False,input_enabled=True,
                                     known_physical_input='PA11 active-low left/boot button -> fire',
                                     other_physical_inputs_integrated=False,
                                     frame_checksums=False,diagnostic_overlay=False,
                                     first_use_asset_CRC=True,
                                     freeze_when_complete=False),
                      pending=['physical cold boot, left-button firing and continuous-play stability',
                               'normal-play frame rate; V48 145 ms is a fixed-tic benchmark result',
                               'movement/right button/touch/sensor hardware integration'])
    if weapon_view_y:
        report.update(comparison_baseline_revision=49,
                      player_sprite_y_scale='Original DOOM Y scale for weapons and muzzle flashes')
    if touch_move:
        report['source_sha256'].update({name:digest((ROOT/'native'/name).read_bytes())
                                       for name in ('t3p_touch.h','t3p_sensors.h','t3p_input.h')})
        report['play_mode']['other_physical_inputs_integrated']=True
        report.update(comparison_baseline_revision=50,
                      touch_input='CST816D drag-to-move/turn, stock PA05/PA06 and PA02, bounded polling',
                      pending=['physical touch response and X/Y axis direction',
                               'physical cold boot, firing while moving, release-to-stop and normal-play frame rate'])
    if dual_keys:
        report['play_mode'].update(
            right_button_input='PB07 active-low -> use, inferred from stock app',
            button_mapping=dict(left_menu='enter',right_menu='escape',
                                both='escape once per 700 ms hold, rearmed when both released'),
            right_key_evidence='Stock 3.0.3 app sets PB07 and PA11 at 0x0c0cccf8 and polls active-low at 0x0c0ccc2a')
        report.update(comparison_baseline_revision=51,
                      expected_display='Continuous DOOM with V51 touch movement, right-button use and two-button menu',
                      pending=['physical right-button door use and simultaneous-button detection',
                               'physical cold boot, firing, touch navigation and menu return'])
    if sensor_controls:
        report['source_sha256'].update({name:digest((ROOT/'native'/name).read_bytes())
                                       for name in ('t3p_tilt.h','t3p_mic.h')})
        report['initialized_hardware']+=['SC7A20-compatible accelerometer on PB02/PB01 I2C',
                                         'BR35 MIC0 on PA04 and audio ADC DMA']
        report['play_mode'].update(
            tilt_input='SC7A20 at 0x19 with the stock FIFO. Eight stable samples set neutral and scale. Touch and menu take priority.',
            mic_input='One fire per onset with peak-to-peak amplitude >= max(4096, 4 * noise + 1024) and activity in at least three 1 ms windows. Cooldown 300 ms.',
            mic_dma='1024 PCM16 samples in SRAM, processed in 512-sample halves with polled completion',
            sensor_diagnostics='Hold right at cold boot for raw XYZ, calibration state, peak, threshold, active windows and trigger count.')
        report.update(comparison_baseline_revision=args.revision-1,
                      restore='Atorch/T3P Firmware Version 3.0.3.bin',
                      expected_display='Continuous DOOM with V52 manual controls, tilt movement and sound-triggered fire',
                      pending=['physical cold boot, sensor initialization and manual controls',
                               'tilt axis direction and microphone sensitivity on the device'])
    if haptics:
        report['source_sha256']['t3p_haptics.h']=digest((ROOT/'native/t3p_haptics.h').read_bytes())
        report['initialized_hardware'].append('BR35 MTPG, PWM channel 1, stock 80% sinking duty')
        report['play_mode'].update(
            haptics='35 ms on local ammunition-consuming fire, 65 ms on local damage after armour. Damage takes priority in the same step. Pulses end by presentation return.',
            haptic_mic_guard='Ignore and drain microphone input during vibration and for 96 ms after stop. This tail also bounds pulse repetition.',
            haptic_diagnostics='Sensor screen row 10 shows 1 after successful PWM timer calibration.')
        report.update(pending=['physical boot, gun recoil and damage vibration',
                               'physical microphone rearm and tilt response during/after vibration'])
    if uac_monitor:
        report['source_sha256']['t3p_baro.h']=digest((ROOT/'native/t3p_baro.h').read_bytes())
        report['initialized_hardware'].append('BMP280-compatible pressure sensor at 0x77 on PB02/PB01 I2C')
        report['play_mode'].update(
            tilt_input='V55 tilt with roll sign corrected and 300/200 engagement/release thresholds out of 1024.',
            uac_monitor='Main-menu page with compensated pressure, altitude relative to the initial eight samples, and an upper-deck message after nine consecutive filtered samples at +3 m or higher.',
            barometer_evidence='Stock 3.0.3 pressure and SC7A20 paths both call 0x0c0cf502; software I2C pin table 0x0c11bcfc has SCL 0x12 and SDA 0x11.')
        report.update(expected_display='Continuous DOOM with tuned tilt and UAC environment monitor',
                      pending=['physical tilt direction and sensitivity',
                               'physical pressure sensor detection, calibrated readout and altitude response'])
    if seamless_view:
        report['source_sha256']['../vendor/dmc100-doom/platform/lcd.h']=digest(
            (ROOT/'vendor/dmc100-doom/platform/lcd.h').read_bytes())
        report['play_mode']['viewport']='Scale 200 rendered rows to 208 at presentation. Overlay HUD message text and retain the 32-row status bar.'
        report.update(expected_display='Continuous DOOM with the 3D view directly above the status bar')
        report['pending'].append('physical view/status-bar seam and HUD message legibility')
    if startup_banner:
        report['play_mode']['startup_banner']=dict(asset=banner_info, draw_size=[216,50],
            minimum_ms=3000, shown_during_game_init=True, skipped_for_sensor_diagnostics=True)
        report['expected_display']='Freedoom startup logo, then continuous DOOM with the seamless status bar'
        report['pending'].append('physical startup banner and transition to gameplay')
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
        doom_name='T3P_NATIVE_DOOM_E1M1_3.0.3.bin' if revision==2 else f'T3P_NATIVE_DOOM_E1M1_V{args.revision}_3.0.3.bin'
        firmware=out/(doom_name if args.doom else f'T3P_NATIVE_BRINGUP_V{revision}_3.0.3.bin');firmware.write_bytes(encoded)
        report.update(image_sha256=digest(encoded),image_bytes=len(encoded),
                      image_path=firmware.relative_to(ROOT).as_posix(),
                      boot_prefix_20_KiB_identical=True,other_entries_and_resource_tail_identical=True,
                      full_redecrypt_matches=True,all_four_nested_CRCs_rebuilt=True)
        if args.doom:
            report.update(hardware_backend_evidence=proven['physical_evidence'],
                          changed_sources_since_hardware_test=changed_sources,
                          doom_execution_verified=False)
        else:report.update(revision=revision,
                      changes=['Stock-matched LCD pin DIE=1 and DIR=0',
                               'Watchdog stopped before boot snapshot',
                               'Timer-independent entry signal and repeating error blink count'],
                      error_blink_codes={'1':'boot handoff validation','2':'timer start',
                                         '3':'LCD initialization or reset delay','4':'drawing or animation'})
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
