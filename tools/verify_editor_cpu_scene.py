#!/usr/bin/env python3
"""Run real editor CPU scene generation with a stubbed GPU/window boundary.
Usage: verify_editor_cpu_scene.py EDITOR_BUILD GAME_ROOT OUTPUT
Requires GNU C++ on Linux. Explicit diagnostic: not part of an ordinary build.
"""
from pathlib import Path
import json,subprocess,sys

def main():
    if len(sys.argv)!=4:raise SystemExit(__doc__)
    build,assets,out=[Path(x).resolve() for x in sys.argv[1:]]
    source=Path(__file__).resolve().parents[1];out.mkdir(parents=True,exist_ok=True)
    flags=['g++','-std=c++20','-O0','-ffunction-sections','-fdata-sections','-DGLFW_INCLUDE_NONE','-I'+str(source/'tests/editor_gui_compile_stub'),'-I'+str(source/'include'),'-I'+str(source/'src/editor')]
    checks=[]
    def run(args,label):
        p=subprocess.run(args,capture_output=True,text=True,timeout=240)
        (out/(label+'.log')).write_text(p.stdout+p.stderr);checks.append({'name':label,'returncode':p.returncode})
        if p.returncode:raise RuntimeError(label+': '+(p.stdout+p.stderr)[-3000:])
        return p.stdout
    objects=[]
    for name in ['editor_app','editor_workspace_ui']:
        obj=out/(name+'.o');objects.append(str(obj))
        run(flags+['-c',str(source/'src/editor'/(name+'.cpp')),'-o',str(obj)],'compile_'+name)
    binary=out/'editor_cpu_scene_probe'
    run(flags+[str(source/'tools/editor_cpu_scene_probe.cpp')]+objects+[str(build/'libthandor_map_core.a'),'-Wl,--gc-sections','-pthread','-ldl','-o',str(binary)],'link')
    binary.chmod(binary.stat().st_mode|0o111)
    print(run([str(binary),str(assets)],'scene').strip().splitlines()[-1])
    (out/'results.json').write_text(json.dumps({'checks':checks,'scope':'Actual EditorApp CPU mesh generation, material residency/alpha, submitted draw ranges and per-unit receipt checks; GPU boundary stubbed'},indent=2)+'\n')
if __name__=='__main__':main()
