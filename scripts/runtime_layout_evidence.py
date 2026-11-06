"""Audit an offline runtime-layout ELF; no device or updater access."""
import struct


def inspect_layout(path,driver_flash_reserve=98304,driver_ram_reserve=32768,tinf_hot_sram=False):
    data=path.read_bytes()
    if data[:6]!=b'\x7fELF\x01\x01' or struct.unpack_from('<H',data,18)[0]!=0xf1:
        raise ValueError('Wrong ELF ABI')
    entry=struct.unpack_from('<I',data,24)[0]
    shoff=struct.unpack_from('<I',data,32)[0]
    shsize,count,names_index=struct.unpack_from('<3H',data,46)
    assert shsize==40
    raw=[struct.unpack_from('<10I',data,shoff+i*40) for i in range(count)]
    def payload(row):return data[row[4]:row[4]+row[5]]
    def string(table,offset):return table[offset:].split(b'\0',1)[0].decode('ascii')
    names=payload(raw[names_index])
    sections={string(names,row[0]):row for row in raw}
    symbols={};function_sizes={}
    for row in raw:
        if row[1]!=2:continue
        strings=payload(raw[row[6]])
        for pos in range(row[4],row[4]+row[5],row[9]):
            name,value,size,info,other,index=struct.unpack_from('<IIIBBH',data,pos)
            if name:
                label=string(strings,name)
                symbols[label]=value
                if info&15==2:function_sizes[label]=size
    ram_begin,ram_end=0x101a80,0x136e00
    flash_begin,flash_end=0xc000100,0xc165e40
    assert entry==flash_begin==symbols['t3p_runtime_start']
    actual_ram=0
    ram_sections=[];flash_sections=[]
    for name,row in sections.items():
        if not row[2]&2 or not row[5]:continue
        start,end=row[3],row[3]+row[5]
        if ram_begin<=start<ram_end:
            assert end<=ram_end
            ram_sections.append((start,end,name))
            if name not in ('.driver_ram_budget','.runtime_stack'):actual_ram+=row[5]
        else:
            assert flash_begin<=start<end<=flash_end,(name,hex(start),hex(end))
            flash_sections.append((start,end,name))
    ram_sections.sort()
    assert all(a[1]<=b[0] for a,b in zip(ram_sections,ram_sections[1:]))
    assert sections['.driver_ram_budget'][5]==driver_ram_reserve
    assert sections['.driver_flash_budget'][5]==driver_flash_reserve
    assert sections['.runtime_stack'][5]==16384
    assert symbols['__runtime_stack_end']==ram_end
    assert symbols['__runtime_heap_start']<=symbols['__runtime_heap_end']
    assert sections['.boot_state'][1]==8 and sections['.boot_state'][5]==76
    assert symbols['t3p_boot_parameters']<symbols['__runtime_bss_start']
    for a,b in [('__runtime_bss_start','__runtime_bss_end'),
                ('__runtime_data_start','__runtime_data_end')]:
        assert symbols[a]%4==symbols[b]%4==0 and symbols[a]<=symbols[b]
    assert flash_begin<=symbols['__runtime_data_load']<flash_end
    assert symbols['__runtime_data_load']%4==0
    game_end=sections['.game'][3]+sections['.game'][5]
    data_flash_start=symbols['__runtime_data_load']
    data_flash_end=data_flash_start+sections['.data'][5]
    assert game_end<=data_flash_start<data_flash_end<=sections['.driver_flash_budget'][3]
    flash_sections.append((data_flash_start,data_flash_end,'.data load image'))
    flash_sections.sort()
    assert all(a[1]<=b[0] for a,b in zip(flash_sections,flash_sections[1:]))
    # Check real linked entry instructions, not just the ELF entry header.
    code=payload(sections['.runtime_entry'])
    assert code.startswith(bytes.fromhex('e300 eeff006e1300 edff006e1300'))
    assert code[14:16]==b'\xc4\xff'
    assert struct.unpack_from('<I',code,16)[0]==symbols['t3p_boot_parameters']
    assert code[20:28]==bytes.fromhex('c060 c161 c262 c363')
    cursor=28
    for target in ['t3p_runtime_signal_entry','t3p_runtime_capture_boot',
                   't3p_runtime_memory_init','t3p_board_start']:
        assert code[cursor:cursor+2]==b'\x80\xff'
        assert entry+cursor+6+struct.unpack_from('<i',code,cursor+2)[0]==symbols[target]
        cursor+=6
    assert code[cursor:]==bytes.fromhex('f79f')
    assert function_sizes['t3p_runtime_memory_init']>0
    hot_functions={}
    if tinf_hot_sram:
        # The two function bodies must be in initialized SRAM, not BSS or a
        # discarded/orphan Flash section. flatten() audits the load image too.
        assert sections['.data'][2]&4  # SHF_EXECINSTR
        for name in ('tinf_decode_symbol','tinf_inflate_block_data'):
            begin=symbols[name];size=function_sizes[name]
            assert size>0 and begin%4==0
            assert symbols['__runtime_data_start']<=begin<begin+size<=symbols['__runtime_data_end']
            hot_functions[name]=dict(address=hex(begin),bytes=size)
    return dict(status='OFFLINE SIZE/PLACEMENT CHECKS PASS; not a boot or hardware test',
                actual_static_ram_bytes=actual_ram,
                explicit_driver_ram_budget_bytes=driver_ram_reserve,explicit_stack_budget_bytes=16384,
                remaining_ram_bytes=symbols['__runtime_heap_end']-symbols['__runtime_heap_start'],
                app_flash_budget_bytes=flash_end-flash_begin,
                flash_used_including_driver_budget=symbols['__runtime_flash_end']-flash_begin,
                remaining_app_flash_bytes=flash_end-symbols['__runtime_flash_end'],
                entry_address=hex(entry),reserved_rom_irq_end=hex(ram_begin),
                update_ram_excluded=[hex(ram_end),'0x137000'],
                boot_parameters_outside_zeroed_bss=True,
                data_source_word_aligned=True,
                linked_cli_stack_parameter_save_and_call_targets_checked=True,
                ram_sections=[dict(begin=hex(a),end=hex(b),name=n) for a,b,n in ram_sections],
                tinf_hot_sram_functions=hot_functions,
                device_execution=False,complete_bsp=False)
