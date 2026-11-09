#!/usr/bin/env python3
"""Compare water relaxation against unchanged supplied native source.
Usage: verify_native_water_relaxation.py NATIVE_SOURCE EDITOR_BUILD GAME_ROOT OUTPUT
GPU/window playback is outside this differential oracle's scope.
"""
from pathlib import Path
import hashlib,json,subprocess,sys

def main():
    if len(sys.argv)!=5:raise SystemExit(__doc__)
    native,build,assets,out=[Path(x).resolve() for x in sys.argv[1:]]
    editor=Path(__file__).resolve().parents[1];out.mkdir(parents=True,exist_ok=True)
    flags=['g++','-std=c++20','-O2','-fwrapv','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-no-pie','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(editor/'tools/native_oracle_stubs'),'-I'+str(native/'include'),'-I'+str(editor/'include'),'-I'+str(editor/'src/editor')]
    checks=[];sources=[]
    def run(args,label):
        p=subprocess.run(args,capture_output=True,text=True,timeout=120)
        (out/(label+'.log')).write_text(p.stdout+p.stderr)
        checks.append({'name':label,'returncode':p.returncode})
        if p.returncode:raise RuntimeError(label+': '+(p.stdout+p.stderr)[-4000:])
        return p.stdout
    objects=[]
    for name in []:
        source=native/'src'/(name+'.cpp');obj=out/(source.stem+'.o');objects.append(str(obj))
        sources.append({'path':'src/'+name+'.cpp','sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
        run(flags+['-c',str(source),'-o',str(obj)],'compile_'+source.stem)
    macros=[]
    for key,name in {'WATER_RELAXATION':'world/terrain/water_relaxation'}.items():
        source=native/'src'/(name+'.cpp');macros+=['-DTHANDOR_NATIVE_'+key+'='+json.dumps(str(source))]
        sources.append({'path':'src/'+name+'.cpp','sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
    binary=out/'native_water_relaxation_oracle'
    run(flags+macros+[str(editor/'tests/terrain_native_water_relaxation_oracle.cpp')]+objects+[str(build/'libthandor_map_core.a'),'-Wl,--gc-sections','-pthread','-o',str(binary)],'link')
    binary.chmod(binary.stat().st_mode|0o111)
    print(run([str(binary),str(assets)],'oracle').strip())
    (out/'results.json').write_text(json.dumps({'checks':checks,'sources':sources,'scope':'256 random fields, 20 alternating in-place forward/reverse sign-gated passes, every water delta checked exactly against unchanged native source; no GPU playback'},indent=2)+'\n')
if __name__=='__main__':main()
