from pathlib import Path
import argparse,json,subprocess
p=argparse.ArgumentParser();p.add_argument('--editor',type=Path);p.add_argument('--core-build',type=Path);a=p.parse_args();r=Path(__file__).resolve().parent;m=json.loads((r/'source_manifest.json').read_text());flags=m['flags'];subprocess.run(flags+['-c',str(r/'grid_probe.cpp'),'-o',str(r/'objects/grid_probe.o')],check=True)
objects=[]
for x in m['sources']:
 rel=x['path'].removeprefix('src/')
 if rel not in m['excluded']:objects.append(str(r/'objects'/(rel.replace('/','_')+'.o')))
subprocess.run([flags[0],'-no-pie','-Wl,--gc-sections',*objects,str(r/'objects/posix_platform.o'),str(r/'objects/grid_probe.o'),'-o',str(r/'native_grid_probe')],check=True)
if a.editor:
 e=a.editor.resolve();s=(e/'tools/simlab_stock_replay.cpp').read_text().replace('            observation.observe(experiment.runs()[0]);','            if(experiment.runs()[0].tick==10){const auto& s=*experiment.runs()[0].retail_scratch;std::ofstream g("lab_grid.bin",std::ios::binary);uint32_t dims[]={s.width,s.height};g.write(reinterpret_cast<const char*>(dims),8);g.write(reinterpret_cast<const char*>(s.state.data()),s.state.size()*4);}\n            observation.observe(experiment.runs()[0]);');(r/'lab_grid_probe.cpp').write_text(s)
 subprocess.run(['g++','-std=c++20','-O2','-I'+str(e/'include'),'-I'+str(e/'tools'),str(r/'lab_grid_probe.cpp'),str(a.core_build.resolve()/'libthandor_map_core.a'),'-pthread','-o',str(r/'lab_grid_probe')],check=True)
