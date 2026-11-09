#!/usr/bin/env python3
"""Audit actual EditorApp CPU shadows against a fresh solve at the same pose.

Usage: verify_runtime_unit_shadows.py BUILD_DIR PCK_ROOT OUTPUT_DIR
The GPU/window boundary is stubbed. A temporary constructor adapter accepts
archives without a game executable; the installed-game UI gate is unchanged.
A successful probe checks ownership/submission, not fixed-light or GPU parity.
Read FIXED_LIGHT_AUDIT records for geometric mismatches.
"""
from pathlib import Path
import json, subprocess, sys

def main():
    if len(sys.argv)!=4: raise SystemExit(__doc__)
    build,assets,out=map(lambda p:Path(p).resolve(),sys.argv[1:])
    source=Path(__file__).resolve().parents[1];out.mkdir(parents=True,exist_ok=True)
    app=(source/'src/editor/editor_app.cpp').read_text()
    before='thandor::AssetCatalog::scan_game_root(game_root)'
    if app.count(before)!=1:raise RuntimeError('constructor adapter needs review')
    (out/'editor_app_archive_adapter.cpp').write_text(app.replace(before,'thandor::AssetCatalog::scan_archive_root(game_root)'))
    flags=['g++','-std=c++20','-O0','-ffunction-sections','-fdata-sections','-DGLFW_INCLUDE_NONE',
        '-I'+str(source/'tests/editor_gui_compile_stub'),'-I'+str(source/'include'),
        '-I'+str(source/'src/editor'),'-I'+str(source/'tools')]
    def run(args,label):
        with (out/(label+'.log')).open('w') as log:
            q=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT)
        if q.returncode:raise RuntimeError(label+' failed; see '+str(out/(label+'.log')))
    objects=[]
    for name,path in [('editor_app',out/'editor_app_archive_adapter.cpp'),
                      ('editor_workspace_ui',source/'src/editor/editor_workspace_ui.cpp'),
                      ('editor_terrain_generator',source/'src/editor/editor_terrain_generator.cpp')]:
        obj=out/(name+'.o');objects.append(str(obj));run(flags+['-c',str(path),'-o',str(obj)],'compile_'+name)
    exe=out/'runtime_shadow_probe'
    run(flags+[str(source/'tools/rendering_runtime_shadow_probe.cpp')]+objects+
        [str(build/'libthandor_map_core.a'),'-Wl,--gc-sections','-pthread','-ldl','-o',str(exe)],'link')
    run([str(exe),str(assets)],'runtime_shadow')
    lines=(out/'runtime_shadow.log').read_text().splitlines()
    audits=[dict(x.split('=',1) for x in line.split()[1:]) for line in lines if line.startswith('FIXED_LIGHT_AUDIT ')]
    result={'cpu_probe_completed':True,'cases':audits,'fixed_light_mismatches':sum(x['cached_matches_fresh']=='0' for x in audits),
        'gpu_verified':False,'scope':'Actual cached dynamic receivers versus fresh same-pose caster solve; constructor/archive and GPU/window adapters only'}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
