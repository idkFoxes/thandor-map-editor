#!/usr/bin/env python3
"""Compare native arithmetic with actual 30-minute faction audit inputs.
Usage: verify_native_observed_economy.py NATIVE_SOURCE EDITOR_BUILD PCK_ROOT FACTIONS_CSV OUTPUT
Model/workspace gates are controlled; this is not an independent whole-game replay.
"""
from pathlib import Path
import hashlib,json,subprocess,sys

def main():
 if len(sys.argv)!=6:raise SystemExit(__doc__)
 native,build,pck,data,out=[Path(x).resolve() for x in sys.argv[1:]];out.mkdir(parents=True,exist_ok=True)
 editor=Path(__file__).resolve().parents[1]
 flags=['g++','-std=c++20','-O2','-fwrapv','-fno-strict-aliasing','-ffp-contract=off','-mms-bitfields','-ffunction-sections','-fdata-sections','-no-pie','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(editor/'tools/native_oracle_stubs'),'-I'+str(native/'include'),'-I'+str(editor/'include')]
 sources=[]
 for key,name in {'PURCHASING':'gameplay/ai/purchasing','WORKSPACES':'gameplay/ai/workspaces','RANDOM':'core/math/random','ARMY_PLACEMENT':'gameplay/army/placement','VECTOR':'core/math/fixed_vector','PLANNING':'gameplay/ai/planning'}.items():
  p=native/'src'/(name+'.cpp');flags+=['-DTHANDOR_NATIVE_'+key+'='+json.dumps(str(p))];sources.append({'path':'src/'+name+'.cpp','sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
 binary=out/'native_observed_economy_oracle';checks=[]
 for name,command in [('compile',flags+[str(editor/'tests/ai_native_observed_economy_oracle.cpp'),str(build/'libthandor_map_core.a'),'-Wl,--gc-sections','-pthread','-o',str(binary)]),('run',[str(binary),str(pck),str(data),str(out/'comparison.csv')])]:
  if name=='run':binary.chmod(binary.stat().st_mode|0o111)
  result=subprocess.run(command,capture_output=True,text=True,timeout=120);(out/(name+'.log')).write_text(result.stdout+result.stderr);checks.append({'name':name,'returncode':result.returncode})
  if result.returncode:raise RuntimeError(f'{name} exited {result.returncode}: '+result.stdout+result.stderr)
  if name=='run':print(result.stdout.strip())
 (out/'results.json').write_text(json.dumps({'sources':sources,'checks':checks,'input_csv_sha256':hashlib.sha256(data.read_bytes()).hexdigest(),'scope':__doc__},indent=2)+'\n')
if __name__=='__main__':main()
