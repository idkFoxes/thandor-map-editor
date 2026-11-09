#!/usr/bin/env python3
"""CPU-only Linux factory/render audit using the real editor and SDK boundary stubs.

Usage: GAME_ROOT CORE_BUILD_DIRECTORY OUTPUT_DIRECTORY [--cases FILE] [--compiler CXX]
Build thandor_map_core from this source tree first. GAME_ROOT needs the original
game executable and PCKs. No game files are bundled and no GPU pixels are checked.
"""
import argparse,json,shutil,subprocess,time
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game_root',type=Path);parser.add_argument('build',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--cases',type=Path);parser.add_argument('--compiler',default=shutil.which('g++')or'g++')
    args=parser.parse_args();source=Path(__file__).resolve().parents[1];out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    cache=(args.build/'CMakeCache.txt').read_text()
    origin=next(line.split('=',1)[1]for line in cache.splitlines()if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL='))
    if Path(origin).resolve()!=source:raise RuntimeError('core build belongs to another source tree; rebuild this checkpoint first')
    core=args.build.resolve()/'libthandor_map_core.a'
    if not core.is_file():raise RuntimeError('build thandor_map_core first')
    flags=[args.compiler,'-std=c++20','-O0','-ffunction-sections','-fdata-sections','-DGLFW_INCLUDE_NONE',
           '-I'+str(source/'tests/editor_gui_compile_stub'),'-I'+str(source/'include'),
           '-I'+str(source/'src/editor'),'-I'+str(source/'tools')]
    objects=[];started=time.monotonic()
    def compile(command,log):
        q=subprocess.run(command,capture_output=True,text=True);(out/log).write_text(q.stdout+q.stderr)
        if q.returncode:raise RuntimeError('compile failed: '+str(out/log))
    for name in ('editor_app','editor_workspace_ui'):
        obj=out/(name+'.o');objects.append(str(obj))
        compile(flags+['-c',str(source/'src/editor'/(name+'.cpp')),'-o',str(obj)],name+'_compile.log')
    executable=out/'rendering_walker_factory_probe'
    compile(flags+[str(source/'tools/rendering_walker_factory_probe.cpp')]+objects+[str(core),'-Wl,--gc-sections','-pthread','-ldl','-o',str(executable)],'probe_link.log')
    cases=args.cases.resolve()if args.cases else source/'tools/fixtures/walker_factory_visibility_cases.tsv'
    with (out/'factory_scene.log').open('w')as log:
        q=subprocess.run([str(executable),str(args.game_root.resolve()),str(cases)],stdout=log,stderr=subprocess.STDOUT)
    lines=(out/'factory_scene.log').read_text().splitlines()
    samples=[dict(word.split('=',1)for word in line.split()[1:]if'='in word)for line in lines if line.startswith('FACTORY_DRAW ')]
    result={'returncode':q.returncode,'elapsed_seconds':time.monotonic()-started,'cases':len(samples),
        'maps':sorted({r['map']for r in samples}),'army_assets':sorted({int(r['arm'])for r in samples}),
        'technology_tiers':sorted({int(r['tier'])for r in samples}),'fog_modes':sorted({int(r['fog'])for r in samples}),
        'root_model_definitions':sorted({int(r['root'])for r in samples}),'gpu_verified':False,
        'scope':'fresh prepaid class13 completion fixtures at reported/map-center locations; all stock walker loadouts added; not original-run replay or GPU playback'}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    assert q.returncode==0 and lines[-1].startswith('WALKER_FACTORY_SCENE PASS ')
    assert len(result['army_assets'])==28 and result['technology_tiers']==[0,1,2]and result['fog_modes']==[0,2]
if __name__=='__main__':main()
