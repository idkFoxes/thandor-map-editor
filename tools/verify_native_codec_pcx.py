#!/usr/bin/env python3
"""Native-source codec/PCX interoperability (no Windows executable needed).
Usage: verify_native_codec_pcx.py NATIVE_SOURCE EDITOR_BUILD OUTPUT [PCK_ROOT]
"""
from pathlib import Path
import hashlib,json,subprocess,sys

def main():
    if len(sys.argv) not in (4,5):raise SystemExit(__doc__)
    native,build,out=[Path(x).resolve() for x in sys.argv[1:4]];out.mkdir(parents=True,exist_ok=True)
    editor=Path(__file__).resolve().parents[1]
    flags=['g++','-std=c++20','-O2','-fwrapv','-fno-strict-aliasing','-ffp-contract=off','-mms-bitfields','-ffunction-sections','-fdata-sections','-no-pie','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(editor/'tools/native_oracle_stubs'),'-I'+str(native/'include'),'-I'+str(editor/'include')]
    sources=[]
    for key,value in {'CODEC':'assets/package/codec','PCX':'graphics/resources/pcx_read'}.items():
        p=native/'src'/(value+'.cpp');flags+=['-DTHANDOR_NATIVE_'+key+'_SOURCE='+json.dumps(str(p))]
        sources.append({'path':'src/'+value+'.cpp','sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    binary=out/'native_codec_pcx_oracle';checks=[]
    commands=[('compile',flags+[str(editor/'tests/assets_native_codec_pcx_oracle.cpp'),str(build/'libthandor_map_core.a'),'-Wl,--gc-sections','-pthread','-o',str(binary)]),('run',[str(binary)]+([str(Path(sys.argv[4]).resolve())] if len(sys.argv)==5 else []))]
    for name,command in commands:
        if name=='run':binary.chmod(binary.stat().st_mode|0o111)
        r=subprocess.run(command,capture_output=True,text=True,timeout=180);(out/(name+'.log')).write_text(r.stdout+r.stderr)
        checks.append({'name':name,'returncode':r.returncode})
        if r.returncode:raise RuntimeError(name+': '+(r.stdout+r.stderr)[-6000:])
        if name=='run':print(r.stdout.strip())
    (out/'results.json').write_text(json.dumps({'sources':sources,'checks':checks,'scope':'Unmodified native encoder/decoder and PCX reader versus editor public decoders; host allocator shim, no full game execution.'},indent=2)+'\n')
if __name__=='__main__':main()
