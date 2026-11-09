#!/usr/bin/env python3
"""Verify live stock depth-caster geometry. Usage: BUILD GAME_ROOT OUTPUT_DIR.
Window/GPU boundary is stubbed; use rendering_directional_shadow_gpu_probe for real pixels."""
from pathlib import Path
import hashlib, json, os, subprocess, sys

def main():
    if len(sys.argv)!=4: raise SystemExit(__doc__)
    build,assets,out=map(lambda p:Path(p).resolve(),sys.argv[1:])
    source=Path(__file__).resolve().parents[1];out.mkdir(parents=True,exist_ok=True)
    app=(source/'src/editor/editor_app.cpp').read_text()
    before='thandor::AssetCatalog::scan_game_root(game_root)'
    if app.count(before)!=1:raise RuntimeError('constructor adapter needs review')
    adapted=app.replace(before,'thandor::AssetCatalog::scan_archive_root(game_root)')
    adapter=out/'editor_app_archive_adapter.cpp'
    if not adapter.exists() or adapter.read_text()!=adapted:adapter.write_text(adapted)
    flags=['g++','-std=c++20','-O0','-ffunction-sections','-fdata-sections','-DGLFW_INCLUDE_NONE',
        '-I'+str(source/'tests/editor_gui_compile_stub'),'-I'+str(source/'include'),
        '-I'+str(source/'src/editor'),'-I'+str(source/'tools')]
    def run(args,label):
        with (out/(label+'.log')).open('w') as log:
            q=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT)
        if q.returncode:raise RuntimeError(label+' failed; see '+str(out/(label+'.log')))
    header_hash=hashlib.sha256()
    for directory in ('include','src/editor','tests/editor_gui_compile_stub'):
        for header in sorted((source/directory).rglob('*.h*')):
            if header.is_file():header_hash.update(header.read_bytes())
    header_hash.update(subprocess.check_output([flags[0],'--version']))
    header_hash.update(repr(flags).encode())
    objects=[]
    for name,path in [('editor_app',out/'editor_app_archive_adapter.cpp'),
                      ('editor_workspace_ui',source/'src/editor/editor_workspace_ui.cpp'),
                      ('editor_terrain_generator',source/'src/editor/editor_terrain_generator.cpp')]:
        obj=out/(name+'.o');objects.append(str(obj))
        signature=hashlib.sha256(header_hash.digest()+path.read_bytes()).hexdigest()
        stamp=out/(name+'.sha256')
        if not obj.exists() or not stamp.exists() or stamp.read_text()!=signature:
            run(flags+['-c',str(path),'-o',str(obj)],'compile_'+name)
            stamp.write_text(signature)
    exe=out/'runtime_shadow_probe'
    run(flags+[str(source/'tools/rendering_directional_shadow_scene_probe.cpp')]+objects+
        [str(build/'libthandor_map_core.a'),'-Wl,--gc-sections','-pthread','-ldl','-o',str(exe)],'link')
    os.environ['THANDOR_SHADOW_SCENE_DIR']=str(out)
    run([str(exe),str(assets)],'runtime_shadow')
    print((out/'runtime_shadow.log').read_text())
if __name__=='__main__':main()
