#!/usr/bin/env python3
"""Execute the original x86 faction loop with hooks assembled from legacy C source.
Usage: RETAIL_EXE ORIGINAL_RECORDER_C FIXED_RECORDER_C OUTPUT
Requires optional Unicorn and GNU as/ld/objcopy. No Windows gameplay is claimed.
"""
from pathlib import Path
import hashlib,json,re,struct,subprocess,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *

def main():
    if len(sys.argv)!=5:raise SystemExit(__doc__)
    exe,old,new,out=[Path(x).resolve() for x in sys.argv[1:]];out.mkdir(parents=True,exist_ok=True)
    data=exe.read_bytes();digest=hashlib.sha256(data).hexdigest()
    if digest!='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969':raise ValueError('unknown original executable')
    pe=struct.unpack_from('<I',data,0x3c)[0];sections=struct.unpack_from('<H',data,pe+6)[0];opt=pe+24
    section_at=opt+struct.unpack_from('<H',data,pe+20)[0];base,size=struct.unpack_from('<I',data,opt+28)[0],struct.unpack_from('<I',data,opt+56)[0]
    uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(size+4095)&~4095);uc.mem_write(base,data[:struct.unpack_from('<I',data,opt+60)[0]])
    for i in range(sections):
        virtual,raw_size,raw=struct.unpack_from('<III',data,section_at+40*i+12)
        if raw_size:uc.mem_write(base+virtual,data[raw:raw+raw_size])
    uc.mem_map(0x60000000,0x100000);uc.mem_map(0x61000000,0x10000);uc.mem_map(0x70000000,0x10000)
    def put(address,*values):uc.mem_write(address,struct.pack('<'+'I'*len(values),*values))
    def word(address):return struct.unpack('<I',uc.mem_read(address,4))[0]
    def assemble(path,label,address):
        text=path.read_text();start=text.index('void ThandorModding_RecordingAiPlannerHook')
        start=text.index('__asm{',start)+6;end=text.index('\n}}',start)
        body=re.sub(r'/\*.*?\*/','',text[start:end],flags=re.S).replace('TH_ORIGINAL_AI_PLANNER','0x0053C810')
        assembly=out/(label+'.s');assembly.write_text('.intel_syntax noprefix\n.code32\n.text\n.globl hook\n.equ gameplay_recording_note_ai_planner,0x61002000\n.equ g_record_original_ai_planner,0x61003000\nhook:\n'+body+'\n')
        for step,args in [('assemble',['as','--32',str(assembly),'-o',str(out/(label+'.o'))]),('link',['ld','-m','elf_i386','-Ttext',hex(address),'-e','hook',str(out/(label+'.o')),'-o',str(out/(label+'.elf'))]),('extract',['objcopy','-O','binary','--only-section=.text',str(out/(label+'.elf')),str(out/(label+'.bin'))])]:
            r=subprocess.run(args,capture_output=True,text=True);(out/(label+'_'+step+'.log')).write_text(r.stdout+r.stderr)
            if r.returncode:raise RuntimeError(label+' '+step+' failed: '+r.stderr)
        code=(out/(label+'.bin')).read_bytes();uc.mem_write(address,code)
        return {'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'assembled_hook_sha256':hashlib.sha256(code).hexdigest(),'hook_bytes':code.hex()}
    identities={'original':assemble(old,'buggy',0x61001000),'fixed':assemble(new,'fixed',0x61001100)}
    original_call=bytes(uc.mem_read(0x5143ae,5));assert original_call==bytes.fromhex('e85d840200')
    assert bytes(uc.mem_read(0x53c810,1))==b'\x50' and bytes(uc.mem_read(0x53c998,4))==bytes.fromhex('58c20800')
    put(0x61003000,0x53c810);put(0x56327c,0x60010000);put(0x50f0c0,0x60000000);put(0x50f0c4,1);put(0x60000058,1)
    put(0x50f0c8,0);put(0x563260,0);put(0x512d58,307)
    calls=[];logged=[];executed=[]
    def instruction(emu,address,size,user):
        if address==0x5143cb:emu.emu_stop()
        elif address==0x61002000:
            sp=emu.reg_read(UC_X86_REG_ESP);logged.append(word(sp+4))
            # A normal C logging helper may clobber the caller-saved registers.
            emu.reg_write(UC_X86_REG_EAX,0xbadf00d);emu.reg_write(UC_X86_REG_ECX,0x12345678);emu.reg_write(UC_X86_REG_EDX,0x87654321)
            emu.reg_write(UC_X86_REG_EIP,word(sp));emu.reg_write(UC_X86_REG_ESP,sp+4)
        elif address==0x53c810:calls.append(word(emu.reg_read(UC_X86_REG_ESP)+4))
        elif address==0x53c85c:
            executed.append(emu.reg_read(UC_X86_REG_EDX))
            # Keep the unchanged original prologue/slot guard/epilogue. Only the
            # selected planner's unrelated world body is skipped in this ABI test.
            emu.reg_write(UC_X86_REG_EIP,0x53c990)
    uc.hook_add(UC_HOOK_CODE,instruction)
    def run(mode,tick,active):
        uc.mem_write(0x512d44,bytes([0]+[int(f in active) for f in range(1,8)]));put(0x512d4c,tick)
        code=original_call if mode=='unhooked' else b'\xe8'+struct.pack('<i',(0x61001000 if mode=='buggy' else 0x61001100)-(0x5143ae+5))
        uc.mem_write(0x5143ae,code)
        # Remove cached translations after changing the call instruction.
        uc.ctl_remove_cache(0x5143ae,0x5143b3)
        for reg in [UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EDI,UC_X86_REG_ESI,UC_X86_REG_EBP]:uc.reg_write(reg,0x1234)
        uc.reg_write(UC_X86_REG_ESP,0x7000f000);uc.reg_write(UC_X86_REG_EFLAGS,2)
        calls.clear();logged.clear();executed.clear();uc.emu_start(0x514350,0x5143cb,count=100000)
        return {'calls':list(calls),'logged':list(logged),'executed':list(executed),'loop_eax':uc.reg_read(UC_X86_REG_EAX)}
    captured=[];checks=0
    for tick in [82,86,90,94,98,106,210,342]:
        rows={mode:run(mode,tick,{1,2,3,4,5}) for mode in ['unhooked','buggy','fixed']}
        assert rows['unhooked']['calls']==rows['fixed']['calls']==[2,3,4,5]
        assert rows['unhooked']['executed']==rows['fixed']['executed']
        assert rows['buggy']['calls']==rows['buggy']['logged']==[2,0x53c811,0x53c811,0x53c811]
        assert rows['fixed']['loop_eax']==rows['unhooked']['loop_eax']==8
        captured.append({'tick':tick,**rows});checks+=3
    for mask in range(64):
        active={1}|{f for f in range(2,8) if mask&(1<<(f-2))}
        for tick in [18,26,34,42,50,58,82,90,98,106,114,122]:
            a=run('unhooked',tick,active);b=run('fixed',tick,active)
            assert a=={**b,'logged':[]},(mask,tick,a,b)
            checks+=2
    result={'retail_sha256':digest,'hook_identities':identities,'x86_loop_executions':checks,
        'captured_layout_cases':captured,'result':'Legacy hook clobbers the retail-preserved EAX faction loop counter. Fixed tail jump preserves the original stack/register ABI.',
        'scope':'Original x86 caller loop and planner prologue/slot guard/epilogue. Selected planner world body skipped; logging C call modeled with caller-saved register clobbers. Not a full Windows game run.'}
    (out/'abi_results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'x86_loop_executions':checks,'captured_90':captured[2],'result':result['result']},indent=2))
if __name__=='__main__':main()
