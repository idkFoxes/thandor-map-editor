#!/usr/bin/env python3
"""Compile original special-site/workspace/RNG bodies. Usage: NATIVE_SOURCE OUTPUT."""
from pathlib import Path
import hashlib,json,subprocess,sys
def main():
    if len(sys.argv)!=3:raise SystemExit(__doc__)
    native,out=(Path(p).resolve() for p in sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
    editor=Path(__file__).resolve().parents[1]
    sources={'PLACEMENT':'gameplay/ai/placement','WORKSPACES':'gameplay/ai/workspaces','RANDOM':'core/math/random','PLANNING':'gameplay/ai/planning','VECTOR':'core/math/fixed_vector'}
    cmd=['g++','-std=c++20','-O2','-fwrapv','-fno-strict-aliasing','-ffp-contract=off','-mms-bitfields','-ffunction-sections','-fdata-sections','-no-pie','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(editor/'tools/native_oracle_stubs'),'-I'+str(native/'include'),'-I'+str(editor/'include')]
    hashes=[]
    for key,name in sources.items():
        p=native/'src'/(name+'.cpp');cmd+=['-DTHANDOR_NATIVE_'+key+'='+json.dumps(str(p))]
        hashes.append({'path':'src/'+name+'.cpp','sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    binary=out/'native_special_site_oracle';cmd+=[str(editor/'tests/placement_native_special_site_oracle.cpp'),'-Wl,--gc-sections','-o',str(binary)]
    checks=[]
    for name,args in [('compile',cmd),('run',[str(binary)])]:
        if name=='run':binary.chmod(binary.stat().st_mode|0o111)
        r=subprocess.run(args,capture_output=True,text=True,timeout=60)
        (out/(name+'.log')).write_text(r.stdout+r.stderr);checks.append({'name':name,'returncode':r.returncode})
        if r.returncode:raise RuntimeError(name+' failed: '+(r.stdout+r.stderr)[-5000:])
        if name=='run':print(r.stdout.strip())
    (out/'results.json').write_text(json.dumps({'sources':hashes,'checks':checks,'scope':'Original candidate branch, first-site scan, cluster inversion, nearest anchor, reverse probe cleanup, weights, workspace kind and native RNG; pressure-building wrapping/enable gates and three-step square roots. Asset placement and probe lifecycle are controlled callbacks, not the full native world loop.'},indent=2)+'\n')
if __name__=='__main__':main()
