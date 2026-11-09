#!/usr/bin/env python3
"""Check placed and produced preview terrain damage through the real CPU editor.
Usage: verify_terrain_damage_preview.py BUILD_DIR PCK_ROOT OUTPUT_DIR
Uses an archive-only constructor adapter and stubbed GPU/window operations.
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
    exe=out/'terrain_damage_preview'
    run(flags+[str(source/'tools/rendering_terrain_damage_preview_probe.cpp')]+objects+
        [str(build/'libthandor_map_core.a'),'-Wl,--gc-sections','-pthread','-ldl','-o',str(exe)],'link')
    run([str(exe),str(assets)],'preview')
    lines=(out/'preview.log').read_text().splitlines()
    assert any(line.startswith('PREVIEW_TERRAIN_DAMAGE PASS') for line in lines),lines[-10:]
    result={'cpu_preview_passed':True,'map_physics_ignores_animation_toggle':True,'gpu_verified':False,'scope':'Real update_simulation, stationary placed and produced class-1 unit, animations disabled'}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
