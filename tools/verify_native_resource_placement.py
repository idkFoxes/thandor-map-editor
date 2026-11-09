#!/usr/bin/env python3
"""Compile unchanged supplied resource planning/support bodies. Usage: NATIVE_SOURCE EDITOR_BUILD OUTPUT."""
from pathlib import Path
import hashlib,json,subprocess,sys

def main():
    if len(sys.argv)!=4:raise SystemExit(__doc__)
    native,build,out=map(lambda x:Path(x).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
    editor=Path(__file__).resolve().parents[1]
    bodies={'PURCHASING':'gameplay/ai/purchasing','WORKSPACES':'gameplay/ai/workspaces','RANDOM':'core/math/random','ARMY_PLACEMENT':'gameplay/army/placement','VECTOR':'core/math/fixed_vector'}
    cmd=['g++','-std=c++20','-O2','-fwrapv','-fno-strict-aliasing','-ffp-contract=off','-mms-bitfields','-ffunction-sections','-fdata-sections','-no-pie','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(editor/'tools/native_oracle_stubs'),'-I'+str(native/'include'),'-I'+str(editor/'include')]
    sources=[]
    for key,value in bodies.items():
        p=native/'src'/(value+'.cpp');cmd+=['-DTHANDOR_NATIVE_'+key+'='+json.dumps(str(p))];sources.append({'path':'src/'+value+'.cpp','sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    binary=out/'native_resource_placement_oracle'
    cmd+=[str(editor/'tests/placement_native_resource_placement_oracle.cpp'),str(build/'libthandor_map_core.a'),'-Wl,--gc-sections','-pthread','-o',str(binary)]
    checks=[]
    for name,args in [('compile',cmd),('run',[str(binary)])]:
        if name=='run':binary.chmod(binary.stat().st_mode|0o111)
        r=subprocess.run(args,capture_output=True,text=True,timeout=60);(out/(name+'.log')).write_text(r.stdout+r.stderr);checks.append({'name':name,'returncode':r.returncode})
        if r.returncode:raise RuntimeError(name+' failed: '+(r.stdout+r.stderr)[-5000:])
        if name=='run':print(r.stdout.strip())
    (out/'native_resource_placement_results.json').write_text(json.dumps({'sources':sources,'checks':checks,'scope':'Original power/workspace/RNG and common building support bodies. Collision, terrain and field blocker callbacks controlled clear. No full game loop.'},indent=2)+'\n')
if __name__=='__main__':main()
