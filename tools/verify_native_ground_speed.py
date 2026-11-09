#!/usr/bin/env python3
"""Compare core speed arithmetic with the original x86 callback, with sound disabled.
Usage: RETAIL_EXE GROUND_SPEED_ORACLE_PROBE OUTPUT_DIRECTORY
Requires optional Unicorn. Does not emulate Windows or full gameplay.
"""
from pathlib import Path
import hashlib,itertools,json,random,struct,subprocess,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EFLAGS

def main():
    if len(sys.argv)!=4:raise SystemExit(__doc__)
    exe,probe,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
    data=exe.read_bytes();digest=hashlib.sha256(data).hexdigest()
    if digest!='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969':
        raise ValueError('unknown original executable')
    pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24
    section=opt+struct.unpack_from('<H',data,pe+20)[0]
    base=struct.unpack_from('<I',data,opt+28)[0];size=struct.unpack_from('<I',data,opt+56)[0]
    uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(size+4095)&~4095)
    for i in range(struct.unpack_from('<H',data,pe+6)[0]):
        virtual,n,raw=struct.unpack_from('<III',data,section+40*i+12)
        if n:uc.mem_write(base+virtual,data[raw:raw+n])
    uc.mem_map(0x62000000,0x10000);uc.mem_map(0x70000000,0x10000)
    definition=0x62002000;root=0x62004000;model=0x62006000;stop=0x62008000;stack=0x70008000
    def put(address,*values):uc.mem_write(address,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
    put(model,definition,root)
    cases=[]
    for speed,current,pitch,heading,azimuth in itertools.product(
        [0,1,84,168,0x7fffffff,0xffffffff], [0,1,41,52,84,999,0xffffffff],
        [10499,10500,10501,14499,14500,14501,0x80000000],
        [0,0x3fff,0x4000,0x4001,0xbfff,0xc000,0xc001], [0,65500]):
        cases.append((speed,41,current,pitch,azimuth,(heading+azimuth)&0xffff,10500,14500))
    rng=random.Random(664)
    for _ in range(5000):cases.append(tuple(rng.getrandbits(32)for _ in range(8)))
    expected=[]
    for speed,acc,current,pitch,azimuth,heading,low,high in cases:
        put(definition+0xc,speed);put(definition+0x18,acc);put(definition+0x24,low);put(definition+0x268,high)
        put(root+0xc,azimuth,pitch,heading);put(model+0x10,current)
        put(stack,stop,0x62000000,model);uc.reg_write(UC_X86_REG_ESP,stack);uc.reg_write(UC_X86_REG_EFLAGS,0x202)
        uc.emu_start(0x529b50,stop,count=150)
        expected.append(struct.unpack('<I',uc.mem_read(model+0x10,4))[0])
        assert uc.reg_read(UC_X86_REG_ESP)==stack+12,'native callback did not return'
    inputs=''.join(' '.join(map(str,row))+'\n'for row in cases)
    (out/'cases.tsv').write_text(inputs)
    q=subprocess.run([str(probe.resolve())],input=inputs,text=True,capture_output=True)
    (out/'probe_stderr.log').write_text(q.stderr)
    if q.returncode:raise RuntimeError('core oracle probe failed')
    actual=list(map(int,q.stdout.split()));assert len(actual)==len(expected)
    mismatches=[{'case':cases[i],'native':a,'core':b}for i,(a,b)in enumerate(zip(expected,actual))if a!=b]
    result={'retail_exe_sha256':digest,'native_entry':'0x00529B50','cases':len(cases),
            'exact':len(cases)-len(mismatches),'mismatches':len(mismatches),'examples':mismatches[:10],
            'scope':'original callback speed state; sound slot zero; no full movement or Windows runtime'}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    assert not mismatches
if __name__=='__main__':main()
