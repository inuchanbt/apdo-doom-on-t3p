"""Fingerprint and label stock PC updater functions without running the EXE."""
import hashlib
import json
from pathlib import Path
import struct
from audit_stock_recovery import ROOT,require

SHA='0e2cde71c3971e9fe1dd8fb066483c54b0cba0cf6a97d0cc2ac42110fa33872e'

def main():
    path=ROOT/'Atorch/01-T3P_PC_Application_V1.0.9/DcTester.exe';b=path.read_bytes()
    require(hashlib.sha256(b).hexdigest()==SHA,'Unexpected PC application')
    pe=struct.unpack_from('<I',b,0x3c)[0];require(b[pe:pe+4]==b'PE\0\0','PE signature')
    machine,ns=struct.unpack_from('<HH',b,pe+4);require(machine==0x14c,'Expectedx86')
    opt=pe+24;optsz=struct.unpack_from('<H',b,pe+20)[0]
    require(struct.unpack_from('<H',b,opt)[0]==0x10b,'ExpectedPE32')
    imagebase=struct.unpack_from('<I',b,opt+28)[0];regions=[]
    for i in range(ns):
        _,vs,rva,rs,off=struct.unpack_from('<8sIIII',b,opt+optsz+i*40)
        regions.append((imagebase+rva,rs,off))
    def offset(va):
        for lo,size,off in regions:
            if lo<=va<lo+size:return off+va-lo
        raise ValueError(f'UnmappedVA{va:#x}')
    def words(va,n):return struct.unpack_from('<'+'I'*n,b,offset(va))
    stringbase=0x4eeb20;first=offset(stringbase)
    nstrings=words(stringbase,4)[3]//16;names=[]
    for i in range(nstrings):
        pos=first+i*16;ref,size,alloc,delta=struct.unpack_from('<iIIi',b,pos)
        require(ref==-1 and alloc==0,'Unexpected Qt string header')
        names.append(b[pos+delta:pos+delta+size].decode('ascii'))
    require(names[0]=='MainDialog','Qt class')
    meta=0x4ee280;h=words(meta,14);require((h[0],h[4],h[5])==(8,82,14),'Qt method metadata')
    # Data-driven method name association; don't label a function from strings
    # occurring nearby in resource blobs or objdump's nearest export labels.
    specs={
        'usbreceivedata':(34,0x43b154,0x4067a0),
        'mytimer_timeout':(38,0x43b116,0x406450),
        'on_upgrade_pushButton_openfile_clicked':(40,0x43b0fc,0x426860),
        'on_upgrade_pushButton_start_clicked':(41,0x43b0ef,0x4117a0),
        'on_disp_btnUpgrade_clicked':(71,0x43af18,0x40adf0),
    }
    methods=[]
    for name,(index,call,target) in specs.items():
        row=words(meta+4*(h[5]+index*5),5);require(names[row[0]]==name,'Qt method index')
        stub=words(0x4ee120+index*4,1)[0];require(stub<=call<stub+40,'Qt dispatch stub')
        pos=offset(call);require(b[pos]==0xe8,'Expected direct x86call')
        actual=call+5+struct.unpack_from('<i',b,pos+1)[0];require(actual==target,'Qt dispatch call')
        methods.append(dict(name=name,index=index,dispatch=hex(stub),call=hex(call),target=hex(target),call_raw=b[pos:pos+5].hex()))
    literals={0x41193f:bytes.fromhex('baaa060000'),0x41196d:bytes.fromhex('baaa000000')}
    evidence=[]
    for va,value in literals.items():
        require(b[offset(va):offset(va)+len(value)]==value,'AA06 header literal')
        evidence.append(dict(address=hex(va),raw_hex=value.hex()))
    result=dict(status='OFFLINE STATIC AUDIT; PC APPLICATION NOT EXECUTED',source_sha256=SHA,
        Qt_class='MainDialog',methods=methods,header_evidence=evidence,
        update_start='0x4117a0 builds AA06 plus ceil(file_bytes/58), CRC16-A001 then sends64bytes at0x4119e7',
        receiver='0x4067a0 handles updater responses and constructs CC data frames from selected file buffer',
        unresolved_header_convention='PC AA06 usesbyte4 asCRC low, receiver readsbyte4 for tail arithmetic. No protocol sender is generated. Aligned stock erase bounds audited for all256 byte4 values.',
        readable_runtime_flash_layout_request_found=False,
        absence_claim='No known safe runtime-layout query established in audited updater paths; not a proof that no such command exists anywhere in the EXE.',
        device_access=False)
    (ROOT/'analysis/stock-pc-updater-audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(source_hash_verified=True,Qt_methods_labeled=len(methods),header_literals_checked=2,PC_application_executed=False,device_access=False,safe_runtime_layout_query_established=False)))

if __name__=='__main__':main()
