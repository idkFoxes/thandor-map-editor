#!/usr/bin/env python3
"""Execute original x86 strategic/candidate instructions in Unicorn, without Windows.
Usage: RETAIL_EXE KI_BIN FULL_PRIMITIVE_EXTRACT_JSON OUTPUT_JSON
Requires optional unicorn; the editor has no dependency on it.
"""
import hashlib,json,random,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EFLAGS

def main():
 if len(sys.argv)!=5:raise SystemExit(__doc__)
 exe,ki_path,evidence,out=map(Path,sys.argv[1:]);data=exe.read_bytes();ki=ki_path.read_bytes()
 digest=hashlib.sha256(data).hexdigest()
 if digest!='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969':raise ValueError('Unknown executable')
 assert len(ki)==512
 pe=struct.unpack_from('<I',data,0x3c)[0];sections=struct.unpack_from('<H',data,pe+6)[0];optional_size=struct.unpack_from('<H',data,pe+20)[0];opt=pe+24
 base=struct.unpack_from('<I',data,opt+28)[0];size=struct.unpack_from('<I',data,opt+56)[0]
 emu=Uc(UC_ARCH_X86,UC_MODE_32);emu.mem_map(base,(size+4095)&~4095);emu.mem_write(base,data[:struct.unpack_from('<I',data,opt+60)[0]])
 for i in range(sections):
  at=opt+optional_size+40*i;virtual,raw_size,raw=struct.unpack_from('<III',data,at+12)
  if raw_size:emu.mem_write(base+virtual,data[raw:raw+raw_size])
 emu.mem_map(0x60000000,0x100000);emu.mem_map(0x70000000,0x10000)
 def put(addr,*words):emu.mem_write(addr,struct.pack('<'+'I'*len(words),*words))
 def read(addr,count=1):return struct.unpack('<'+'I'*count,emu.mem_read(addr,count*4))
 facts=json.loads(evidence.read_text())['fact_changes'];row=next(x for x in reversed(facts) if x['faction']==2 and x['tick']<=342);w=row['words'];faction=0x50f340+2*0x740
 emu.mem_write(faction,struct.pack('<48I',*w[2:50]));emu.mem_write(faction+0x6e0,struct.pack('<24I',*w[50:74]))
 emu.mem_write(0x60000000,ki);put(0x53c5e0,0x60000000)
 # Retained in-production ARM330 presence, absent ARM301/321..323. No graphics/world callbacks.
 put(0x537960,0x60001000,1);put(0x60001000,0,330)
 put(0x537410,0x60002000);put(0x4030a0,0x4030b0)
 for i,id in enumerate([301,321,322,323]):
  p=0x60004000+i*128;put(0x519778+4*i,p);put(p,128,0,id);put(p+0x14,0x111);put(p+0x28,12000 if id==301 else 17600)
 sentinel=0x7000f000
 def call(address,*args):
  sp=0x7000e000-4*(len(args)+1);put(sp,sentinel,*args);emu.reg_write(UC_X86_REG_ESP,sp);emu.reg_write(UC_X86_REG_EFLAGS,2);emu.emu_start(address,sentinel,count=100000)
  assert emu.reg_read(UC_X86_REG_ESP)==0x7000e000,'Unbalanced original function stack'
 rng=random.Random(0x656);count=10000;first=None
 for i in range(count):
  put(0x537414,0);put(0x4030a4,rng.randrange(1<<32))
  call(0x53ac20,2,0);call(0x53b070,2,0);call(0x5374b0,201,struct.unpack_from('<I',ki,13*4)[0],2);call(0x537570)
  n=read(0x537414)[0];entries=[read(0x60002000+j*8,2) for j in range(n)]
  assert n==3 and entries[0][1]&65535==301,(i,entries)
  if first is None:first=[{'id':id&65535,'packed_score':score} for score,id in entries]
 # Controlled final four draws inferred from the admitted post-pass seed; this is NOT an autonomous replay.
 end_seed=1835659211
 before=end_seed
 for _ in range(4):before=((before-3434)*pow(1089,-1,1<<32))&0xffffffff
 put(0x537414,0);put(0x4030a4,before)
 call(0x53ac20,2,0);call(0x53b070,2,0);call(0x5374b0,201,struct.unpack_from('<I',ki,13*4)[0],2);call(0x537570)
 n=read(0x537414)[0];end_entries=[read(0x60002000+j*8,2) for j in range(n)]
 controlled={'post_pass_seed':end_seed,'reconstructed_pre_four_draws':before,'actual_post_seed':read(0x4030a4)[0],'candidates':[{'id':id&65535,'score':score} for score,id in end_entries],'scope':'Original x86 controlled-input conditional four-draw suffix; not recovered upstream RNG history.'}
 assert read(0x4030a4)[0]==end_seed
 out.with_name('recorded_seed_scoring_diagnostic.json').write_text(json.dumps(controlled,indent=2)+'\n')
 result={'retail_sha256':digest,'ki_sha256':hashlib.sha256(ki).hexdigest(),'fact_source_tick':row['tick'],'compared_tick':342,'seeds':count,'first_candidates':first,'result':'Original x86 instructions also rank ARM301 first with these supplied inputs. The recording does not supply live KI/workspace/registry snapshots.','scope':'Original stage building, weighted laboratory, score insertion, sorting and their original lookup/capacity/RNG helpers execute unchanged. Controlled ARM330-only workspace and enabled registry entries; no full game loop.'}
 out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
