#!/usr/bin/env python3
"""Check general-site choice against original x86 +B8/+BC reads.
Usage: RETAIL_EXE AI_SITE_ORIGIN_PROBE OUTPUT_DIRECTORY
Optional Unicorn; no full Windows/game emulation.
"""
from pathlib import Path
import hashlib,json,random,struct,subprocess,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EFLAGS,UC_X86_REG_EAX,UC_X86_REG_EBX

def main():
    exe,probe,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
    data=exe.read_bytes();sha=hashlib.sha256(data).hexdigest()
    assert sha=='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969','unknown executable'
    pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24;section=opt+struct.unpack_from('<H',data,pe+20)[0]
    base=struct.unpack_from('<I',data,opt+28)[0];size=struct.unpack_from('<I',data,opt+56)[0]
    uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(size+4095)&~4095)
    for i in range(struct.unpack_from('<H',data,pe+6)[0]):
        virtual,n,raw=struct.unpack_from('<III',data,section+40*i+12)
        if n:uc.mem_write(base+virtual,data[raw:raw+n])
    uc.mem_map(0x62000000,0x10000);uc.mem_map(0x70000000,0x10000)
    army=0x62000000;model=0x62001000;ki=0x62002000;sites=0x62004000;stop=0x62008000;stack=0x70008000
    def put(at,*v):uc.mem_write(at,struct.pack('<'+'I'*len(v),*(x&0xffffffff for x in v)))
    def signed(v):return (v+0x80000000)%0x100000000-0x80000000
    put(0x53c5e0,ki);put(0x537988,sites);put(army+4,model)
    cases=[]
    # Paired model poses with the same stored goal must retain the same choice.
    for path in [(0,0),(24576,0),(0,-32768)]:
        for model_pos in [(0,0),(24576,0),(100000,-100000)]:
            cases.append((*model_pos,*path,65536,4096,1,[(0,0,0),(24576,0,0)]))
    cases.extend([(0,0,0,0,65536,4096,1,[]),(0,0,0,0,65536,4096,1,[(1,1,0),(1,1,0)])])
    rng=random.Random(666)
    for i in range(6000):
        word=lambda:signed(rng.getrandbits(32))
        coord=lambda:rng.randint(-400000,400000)
        values=[word()for _ in range(7)] if i%2 else [coord(),coord(),coord(),coord(),rng.randint(0,1000000),rng.randint(-4096,4096),rng.randint(0,300)]
        rows=[(coord(),coord(),word())for _ in range(rng.randrange(33))]
        cases.append((*values,rows))
    expected=[];inputs=[]
    for mx,my,px,py,bias,scale,weight,rows in cases:
        put(model, mx,my);put(model+0x34,mx,my);put(army+0xb8,px,py);put(army+0x80,weight)
        put(ki+0x8c,bias);put(ki+0x9c,scale);put(0x53798c,len(rows))
        for i,(x,y,score)in enumerate(rows):put(sites+i*16,x,y,score,0)
        put(stack,stop,0,0,army);uc.reg_write(UC_X86_REG_ESP,stack);uc.reg_write(UC_X86_REG_EFLAGS,0x202)
        uc.emu_start(0x53b1d0,stop,count=3000)
        result=uc.reg_read(UC_X86_REG_EBX);index=(result-sites)//16 if result else -1
        expected.append((signed(uc.reg_read(UC_X86_REG_EAX)),index))
        assert uc.reg_read(UC_X86_REG_ESP)==stack+16,'original function did not return'
        inputs.append(' '.join(map(str,[mx,my,px,py,bias,scale,weight,len(rows)]+[v for row in rows for v in row]))+'\n')
    text=''.join(inputs);(out/'cases.tsv').write_text(text)
    q=subprocess.run([str(probe.resolve())],input=text,text=True,capture_output=True)
    (out/'probe_stderr.log').write_text(q.stderr);assert q.returncode==0
    actual=[tuple(map(int,l.split()))for l in q.stdout.splitlines()];assert len(actual)==len(expected)
    wrong=[{'case':i,'native':a,'core':b}for i,(a,b)in enumerate(zip(expected,actual))if a!=b]
    result={'retail_exe_sha256':sha,'native_entry':'0x0053B1D0','native_coordinate_offsets':['ArmyRuntime+0xB8','ArmyRuntime+0xBC'],'cases':len(cases),'exact':len(cases)-len(wrong),'mismatches':len(wrong),'examples':wrong[:10],'scope':'original general-site scorer/selection including wrap and ties; not complete AI/Windows runtime'}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));assert not wrong
if __name__=='__main__':main()
