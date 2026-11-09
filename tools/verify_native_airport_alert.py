#!/usr/bin/env python3
"""Compile original audio/capability and AI movement-class callbacks. Usage: NATIVE_SOURCE BUILD OUTPUT."""
from pathlib import Path
import hashlib,json,subprocess,sys

def main():
    if len(sys.argv)!=4:raise SystemExit(__doc__)
    native,build,out=(Path(x).resolve() for x in sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
    editor=Path(__file__).resolve().parents[1]
    sources={'AUDIO':'gameplay/army/audio','FACTION_RUNTIME':'gameplay/faction/runtime','PLANNING':'gameplay/ai/planning'}
    cmd=['g++','-std=c++20','-O2','-fwrapv','-fno-strict-aliasing','-ffp-contract=off','-mms-bitfields','-ffunction-sections','-fdata-sections','-no-pie','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(editor/'tools/native_oracle_stubs'),'-I'+str(native/'include'),'-I'+str(editor/'include')]
    hashes=[]
    for key,name in sources.items():
        path=native/'src'/(name+'.cpp');cmd+=['-DTHANDOR_NATIVE_'+key+'='+json.dumps(str(path))]
        hashes.append({'path':'src/'+name+'.cpp','sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    binary=out/'native_airport_alert_oracle'
    cmd+=[str(editor/'tests/aviation_native_airport_alert_oracle.cpp'),str(build/'libthandor_map_core.a'),'-Wl,--gc-sections','-pthread','-o',str(binary)]
    checks=[]
    for label,args in [('compile',cmd),('run',[str(binary)])]:
        if label=='run':binary.chmod(binary.stat().st_mode|0o111)
        result=subprocess.run(args,capture_output=True,text=True,timeout=60)
        (out/(label+'.log')).write_text(result.stdout+result.stderr)
        checks.append({'name':label,'returncode':result.returncode})
        if result.returncode:raise RuntimeError(label+' failed: '+(result.stdout+result.stderr)[-5000:])
        if label=='run':print(result.stdout.strip())
    (out/'results.json').write_text(json.dumps({'sources':hashes,'checks':checks,'scope':'Original mapped-terrain sound callback and source faction capability predicate; synthetic world sound slots and projected cell occupancy; batch mission countdown supplied explicitly. Original active-grid-class collector compared on 0..85 generated roots with native 64-unit workspace capacity.'},indent=2)+'\n')
if __name__=='__main__':main()
