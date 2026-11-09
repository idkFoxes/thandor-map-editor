#!/usr/bin/env python3
"""Compare original research lifecycle, availability, assignment and exchange with Lab.
Usage: NATIVE_SOURCE EDITOR_BUILD OUTPUT
"""
from pathlib import Path
import hashlib,json,subprocess,sys

def main():
    if len(sys.argv)!=4:raise SystemExit(__doc__)
    native,build,out=map(lambda x:Path(x).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
    editor=Path(__file__).resolve().parents[1]
    # Keep the callback verbatim, omitting the unrelated dispatch tables whose initializers
    # otherwise pull every graphics/audio/class implementation into this isolated executable.
    original=native/'src/gameplay/army/class_dispatch.cpp'
    text=original.read_text();start=text.index('void ArmyRuntimeHierarchy_UpdateProgressAndClassCallbacksRecursive\n')
    end=text.index('\n/* Runs the sound callback',start);body=text[start:end]
    extracted=out/'original_hierarchy_callback.inc';extracted.write_text(body)
    bodies={'TECHNOLOGY':'gameplay/technology/runtime','RELATIONS':'gameplay/faction/relations',
            'PURCHASING':'gameplay/ai/purchasing','WORKSPACES':'gameplay/ai/workspaces','ARMY_STOCK':'gameplay/faction/army_stock'}
    cmd=['g++','-std=c++20','-O2','-fwrapv','-fno-strict-aliasing','-ffp-contract=off','-mms-bitfields','-ffunction-sections','-fdata-sections','-no-pie','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(editor/'tools/native_oracle_stubs'),'-I'+str(native/'include'),'-I'+str(editor/'include')]
    sources=[{'path':str(original.relative_to(native)),'sha256':hashlib.sha256(original.read_bytes()).hexdigest(),'verbatim_callback_sha256':hashlib.sha256(body.encode()).hexdigest()}]
    for key,value in bodies.items():
        p=native/'src'/(value+'.cpp');cmd+=['-DTHANDOR_NATIVE_'+key+'='+json.dumps(str(p))];sources.append({'path':'src/'+value+'.cpp','sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    cmd+=['-DTHANDOR_NATIVE_CALLBACK='+json.dumps(str(extracted))]
    binary=out/'native_research_lifecycle_oracle'
    cmd+=[str(editor/'tests/research_native_research_lifecycle_oracle.cpp'),str(build/'libthandor_map_core.a'),'-Wl,--gc-sections','-pthread','-o',str(binary)]
    checks=[]
    for name,args in [('compile',cmd),('run',[str(binary)])]:
        if name=='run':binary.chmod(binary.stat().st_mode|0o111)
        r=subprocess.run(args,capture_output=True,text=True,timeout=60);(out/(name+'.log')).write_text(r.stdout+r.stderr);checks.append({'name':name,'returncode':r.returncode})
        if r.returncode:raise RuntimeError(name+' failed: '+(r.stdout+r.stderr)[-6000:])
        if name=='run':print(r.stdout.strip())
    (out/'native_research_lifecycle_results.json').write_text(json.dumps({'sources':sources,'checks':checks,'scope':'Verbatim original hierarchy callback with class update no-op; original availability, purchase and assignment, unlock recursion and relation exchange. UI/catalog/variant rendering are observed no-ops. No full Windows game loop.'},indent=2)+'\n')
if __name__=='__main__':main()
