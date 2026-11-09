#!/usr/bin/env python3
"""Run the production local-capture stubs against original x86 handler prologues.
Usage: RETAIL_EXE OUTPUT_DIRECTORY [CXX=g++]
Requires optional Unicorn. This is byte/ABI validation, not Windows gameplay.
"""
from pathlib import Path
import ctypes,hashlib,json,random,struct,subprocess,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *

def main():
    if len(sys.argv)not in (3,4):raise SystemExit(__doc__)
    exe,out=map(Path,sys.argv[1:3]);out=out.resolve();out.mkdir(parents=True,exist_ok=True)
    source=Path(__file__).resolve().parent/'legacy_recorder_local_capture'
    metadata=json.loads((source/'recording_local_command_sites.json').read_text())
    data=exe.read_bytes();digest=hashlib.sha256(data).hexdigest()
    if digest!=metadata['retail_exe_sha256']:raise ValueError('unknown original executable')
    shim=out/'capture_shim.cpp';shim.write_text('''
#include "recording_local_command_sites.h"
extern "C" unsigned build(unsigned index,unsigned base,unsigned logger,unsigned char* out){
 return threc_build_local_stub(out,64,base,logger,&threc_local_handlers[index]);}
extern "C" int allowed(int enabled,unsigned role,unsigned local,unsigned player,unsigned pc,unsigned code){
 return threc_local_capture_allowed(enabled,role,local,player,pc,code,threc_local_callers,THREC_LOCAL_CALLER_COUNT);}
extern "C" void packet(unsigned tick,unsigned faction,unsigned code,const unsigned* stack,unsigned* out){
 threc_local_packet(out,tick,faction,code,stack);}
''')
    command=[sys.argv[3]if len(sys.argv)==4 else'g++','-std=c++20','-O2','-shared','-fPIC','-I',str(source),str(shim),'-o',str(out/'capture_shim.so')]
    q=subprocess.run(command,capture_output=True,text=True);(out/'compile.log').write_text(q.stdout+q.stderr)
    if q.returncode:raise RuntimeError('portable production-helper compile failed')
    lib=ctypes.CDLL(str(out/'capture_shim.so'));u32=ctypes.c_uint32
    lib.build.argtypes=[u32,u32,u32,ctypes.POINTER(ctypes.c_ubyte)];lib.build.restype=u32
    lib.allowed.argtypes=[ctypes.c_int,u32,u32,u32,u32,u32];lib.allowed.restype=ctypes.c_int
    lib.packet.argtypes=[u32,u32,u32,ctypes.POINTER(u32),ctypes.POINTER(u32)]
    pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24
    section=opt+struct.unpack_from('<H',data,pe+20)[0];n=struct.unpack_from('<H',data,pe+6)[0]
    base=struct.unpack_from('<I',data,opt+28)[0];size=struct.unpack_from('<I',data,opt+56)[0]
    uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(size+4095)&~4095)
    uc.mem_write(base,data[:struct.unpack_from('<I',data,opt+60)[0]])
    for i in range(n):
        virtual,raw_size,raw=struct.unpack_from('<III',data,section+40*i+12)
        if raw_size:uc.mem_write(base+virtual,data[raw:raw+raw_size])
    uc.mem_map(0x61000000,0x10000);uc.mem_map(0x62000000,0x10000);uc.mem_map(0x70000000,0x10000)
    logger=0x6100f000;stack_base=0x70008000
    registers=[UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESI,
               UC_X86_REG_EDI,UC_X86_REG_EBP,UC_X86_REG_ESP,UC_X86_REG_EFLAGS]
    def put(address,*values):uc.mem_write(address,struct.pack('<'+'I'*len(values),*values))
    def words(address,count):return struct.unpack('<'+'I'*count,uc.mem_read(address,count*4))
    for caller in metadata['sites']:
        code=bytes.fromhex(caller['opcode'])
        assert bytes(uc.mem_read(caller['return_pc']-len(code),len(code)))==code
        command=caller['handler_code']if caller['handler_code']!=0xffffffff else 0xd40
        assert lib.allowed(1,0,7,7,caller['return_pc'],command)
    def fresh(values,stack,flags):
        uc.mem_write(stack_base-256,bytes(512));put(stack_base,*stack)
        for reg,value in zip(registers[:-2],values):uc.reg_write(reg,value)
        uc.reg_write(UC_X86_REG_ESP,stack_base);uc.reg_write(UC_X86_REG_EFLAGS,flags)
    def snapshot():
        regs=[uc.reg_read(reg)for reg in registers];sp=regs[-2]
        return regs,bytes(uc.mem_read(sp,stack_base+20-sp))
    context={'enabled':1,'packets':[],'demo':False,'applied':[],'entry_stack':0}
    def on_code(machine,address,length,_):
        if address==logger:
            sp=uc.reg_read(UC_X86_REG_ESP);ret,code,original_stack=words(sp,3)
            stack=words(original_stack,5);role=words(0x50f0c8,1)[0];local=words(0x50f0ac,1)[0]
            if lib.allowed(context['enabled'],role,local,stack[1],stack[0],code):
                packet=(u32*8)();lib.packet(123,2,code,(u32*5)(*stack),packet);context['packets'].append(list(packet))
            # Deliberately clobber all non-stack GP registers and arithmetic flags.
            for index,reg in enumerate(registers[:-2]):uc.reg_write(reg,0xaabb0000+index)
            uc.reg_write(UC_X86_REG_EFLAGS,0x203);uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,ret)
        elif context['demo']:
            h=context['demo_handler']
            if address==h['address']:
                sp=uc.reg_read(UC_X86_REG_ESP);context['entry_stack']=sp
                context['entry_registers']=[uc.reg_read(r)for r in registers]
                context['applied'].append(list(words(sp,5)))
            elif address==h['address']+h['size']:
                # Stop native gameplay here: this controlled callee returns after
                # its real prologue. No assets/GUI/gameplay are emulated.
                for reg,value in zip(registers,context['entry_registers']):uc.reg_write(reg,value)
                sp=context['entry_stack'];ret=words(sp,1)[0]
                uc.reg_write(UC_X86_REG_ESP,sp+20);uc.reg_write(UC_X86_REG_EIP,ret)
    uc.hook_add(UC_HOOK_CODE,on_code)
    rng=random.Random(663);cases=0;captures=0;exclusions=0;identities=[]
    for index,h in enumerate(metadata['handlers']):
        entry=h['address'];original=bytes.fromhex(h['bytes']);assert bytes(uc.mem_read(entry,h['size']))==original
        address=0x61001000+index*64;buf=(ctypes.c_ubyte*64)();length=lib.build(index,address,logger,buf)
        code=bytes(buf[:length]);uc.mem_write(address,code)
        identities.append({'command':h['code'],'entry':entry,'stub':address,'bytes':code.hex()})
        caller=next((c for c in metadata['sites']if c['handler_code']==h['code']),
                    next(c for c in metadata['sites']if c['handler_code']==0xffffffff))
        wrong=next(c for c in metadata['sites']if c['handler_code']not in (h['code'],0xffffffff))
        scenarios=[(1,0,7,7,caller['return_pc'],1),(0,0,7,7,caller['return_pc'],0),
          (1,1,7,7,caller['return_pc'],0),(1,2,7,7,caller['return_pc'],0),(1,3,7,7,caller['return_pc'],0),
          (1,0,7,0xffffffff,caller['return_pc'],0),(1,0,7,7,0x53c811,0),
          (1,0,7,7,wrong['return_pc'],0),(1,0,7,8,caller['return_pc'],0),
          (1,0,255,255,caller['return_pc'],0),(1,4,7,7,caller['return_pc'],1),
          (1,0,0,0,caller['return_pc'],1),(1,0,254,254,caller['return_pc'],1)]
        for vector in range(3):
            values=[rng.getrandbits(32)for _ in registers[:-2]];flags=0x202|(rng.getrandbits(12)&0x8d5)
            payload=[rng.getrandbits(32)for _ in range(3)]
            for enabled,role,local,player,return_pc,expected in scenarios:
                stack=[return_pc,player,*payload];put(0x50f0c8,role);put(0x50f0ac,local)
                context.update(enabled=enabled,packets=[])
                uc.mem_write(entry,original);uc.ctl_remove_cache(entry,entry+16)
                fresh(values,stack,flags);uc.emu_start(entry,entry+h['size'],count=100)
                before=snapshot()
                uc.mem_write(entry,b'\xe9'+struct.pack('<I',(address-entry-5)&0xffffffff)+b'\x90'*(h['size']-5))
                uc.ctl_remove_cache(entry,entry+16)
                fresh(values,stack,flags);uc.emu_start(entry,entry+h['size'],count=100)
                assert snapshot()==before,(h['name'],vector,role,player,'ABI changed')
                assert len(context['packets'])==expected,(h['name'],context['packets'],expected)
                if expected:
                    assert context['packets'][0]==[1,123,player,2,h['code'],*payload]
                    captures+=1
                else:exclusions+=1
                cases+=1
        uc.mem_write(entry,original);uc.ctl_remove_cache(entry,entry+16)
    # Execute the original build-catalog instruction branch, not a reconstructed
    # caller: role=0 calls the handler directly; role=1 appends to the queue.
    h=next(h for h in metadata['handlers']if h['code']==0xfe0);index=metadata['handlers'].index(h)
    address=0x61001000+index*64;entry=h['address'];original=bytes.fromhex(h['bytes']);demos=[]
    for hooked in (False,True):
        for role in (0,1):
            put(0x50f0c8,role);put(0x50f0ac,7);put(0x55f0c0,0x55efc0)
            uc.mem_write(0x55efc0,bytes(256));put(0x62000a80,2);put(0x62002008,330)
            if hooked:uc.mem_write(entry,b'\xe9'+struct.pack('<I',(address-entry-5)&0xffffffff)+b'\x90'*(h['size']-5))
            else:uc.mem_write(entry,original)
            uc.ctl_remove_cache(entry,entry+16)
            fresh([0x62000000,0x62002000,3,4,5,6,7],[0,0,0,0,0],0x202)
            context.update(enabled=1,packets=[],applied=[],demo=True,demo_handler=h)
            uc.emu_start(0x56d110,0x56d16e,count=300)
            assert len(context['packets'])==int(hooked and role==0)
            if role==0:
                assert context['applied']==[[0x56d13e,7,1,330,2]]
                if hooked:assert context['packets']==[[1,123,7,2,0xfe0,1,330,2]]
            else:assert words(0x55efc0,4)==((0xfe0<<8)|7,2,330,1)
            demos.append({'hooked':hooked,'network_role':role,'local_records':len(context['packets']),
                          'handler_calls':context['applied'],'queued_record':list(words(0x55efc0,4))if role else None})
    uc.mem_write(entry,original);uc.ctl_remove_cache(entry,entry+16)
    indirect=[]
    caller=next(c for c in metadata['sites']if c['handler_code']==0xffffffff)
    assert caller['opcode']=='ffd0' # Original world-input dynamic command call: call eax.
    for command in (0xd40,0xd70,0x13a0):
        h=next(h for h in metadata['handlers']if h['code']==command)
        index=metadata['handlers'].index(h);entry=h['address'];original=bytes.fromhex(h['bytes'])
        address=0x61001000+index*64
        for hooked in (False,True):
            uc.mem_write(entry,b'\xe9'+struct.pack('<I',(address-entry-5)&0xffffffff)+b'\x90'*(h['size']-5)if hooked else original)
            uc.ctl_remove_cache(entry,entry+16);put(0x50f0c8,0);put(0x50f0ac,7)
            fresh([entry,2,3,4,5,6,7],[7,0x80000001,0x76543210,0xffffffff,0],0x202)
            context.update(enabled=1,packets=[],applied=[],demo=True,demo_handler=h)
            uc.emu_start(caller['return_pc']-2,caller['return_pc'],count=100)
            assert len(context['packets'])==int(hooked)
            assert context['applied']==[[caller['return_pc'],7,0x80000001,0x76543210,0xffffffff]]
            if hooked:assert context['packets']==[[1,123,7,2,command,0x80000001,0x76543210,0xffffffff]]
            indirect.append({'command':command,'hooked':hooked,'local_records':len(context['packets']),'return_pc':caller['return_pc']})
        uc.mem_write(entry,original);uc.ctl_remove_cache(entry,entry+16)
    context['demo']=False
    result={'retail_exe_sha256':digest,'production_header_sha256':hashlib.sha256((source/'recording_local_command_hook.h').read_bytes()).hexdigest(),
      'handlers':len(metadata['handlers']),'approved_sites':len(metadata['sites']),'abi_cases':cases,
      'captured_cases':captures,'excluded_cases':exclusions,'GP_flags_live_stack_equal':True,'UCMD_payload_order_equal':True,
      'original_catalog_branch':demos,'original_indirect_world_command':indirect,'stubs':identities,
      'scope':'Original x86 UI branch, queue code and 77 handler prologues with controlled logger and callee-body boundary. Not Windows DLL link, full handler effects or gameplay validation.'}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items()if k!='stubs'},indent=2))
if __name__=='__main__':main()
