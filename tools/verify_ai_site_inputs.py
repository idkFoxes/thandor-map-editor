#!/usr/bin/env python3
"""Observe native/Lab workspace-05 inputs without changing gameplay or consuming RNG."""
from pathlib import Path
import argparse,subprocess,os,json,struct,collections
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game-data',type=Path,required=True);p.add_argument('--native-runner',type=Path,required=True);p.add_argument('--core',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();e=Path(__file__).resolve().parent.parent;o=a.output.resolve();o.mkdir(parents=True,exist_ok=True)
s=(e/'tools/simlab_stock_replay.cpp').read_text();core=(e/'src/core/simulation_lab.cpp').read_text()
needle='    auto workspaces = lab_rebuild_placement_workspaces(run,faction,field,true);'
observer=r'''if(run.tick==10||run.tick==18||run.tick==138||run.tick==146){for(size_t j=0;j<workspaces.general05.size();++j){const auto&v=workspaces.general05[j];std::fprintf(stderr,"WS05,%u,%zu,%d,%d,%d\n",run.tick,j,float_to_q12(v.point.x),float_to_q12(v.point.y),v.score);}}'''
assert core.count(needle)==1;core=core.replace(needle,needle+'\n'+observer)
s='#include <cstdio>\n'+core+'\n'+s
probe=r'''const auto&r=experiment.runs()[0];if(r.tick==10||r.tick==18||r.tick==138||r.tick==146){
 const uint32_t faction=(r.tick==18||r.tick==146)?2:1;const auto ws=thandor::lab_rebuild_placement_workspaces(r,faction,field,true);
 std::cerr<<"WS05_COUNT,"<<r.tick<<','<<ws.general05.size()<<'\n';for(size_t j=0;j<ws.general05.size();++j){const auto&v=ws.general05[j];std::cerr<<"WS05_POST,"<<r.tick<<','<<j<<','<<float_to_q12(v.point.x)<<','<<float_to_q12(v.point.y)<<','<<v.score<<'\n';}
 const auto own=thandor::lab_ai_owner_workspaces_v718(r,field,faction);for(size_t j=0;j<own.units01.size();++j){const auto&v=r.entities[own.units01[j]];const auto m=thandor::lab_ai_model_point_v719(v.actor.position);const auto path=v.ai_path_coordinate_q12.value_or(m);std::cerr<<"WS01,"<<r.tick<<','<<j<<','<<v.army_asset_id<<','<<m.x<<','<<m.y<<','<<path.x<<','<<path.y<<'\n';}
 const auto active=thandor::simulation_lab_ai_active_grid_classes(r,faction);std::cerr<<"ACTIVE,"<<r.tick;for(auto v:active)std::cerr<<','<<v;std::cerr<<'\n';
 const auto dump=[&](const char*kind,uint32_t w,uint32_t h,const auto&values){const auto path=std::filesystem::path(audit_dir)/(std::string("lab_")+kind+std::to_string(r.tick)+".bin");std::ofstream f(path,std::ios::binary);uint32_t dims[]{w,h};f.write(reinterpret_cast<const char*>(dims),8);f.write(reinterpret_cast<const char*>(values.data()),std::streamsize(values.size()*sizeof(values[0])));if(!f)throw std::runtime_error("diagnostic binary write failed");};
 dump("grid",r.retail_scratch->width,r.retail_scratch->height,r.retail_scratch->state);dump("occupancy",field.width(),field.height(),r.retail_scratch->field_occupancy_masks);
}
'''
needle='            observation.observe(experiment.runs()[0]);\n            if';assert needle in s;s=s.replace(needle,'            observation.observe(experiment.runs()[0]);\n'+probe+'            if');(o/'lab_probe.cpp').write_text(s)
with (o/'build.log').open('w') as f:subprocess.run([os.getenv('CXX','g++'),'-std=c++20','-O1','-DNDEBUG','-I'+str(e/'include'),'-I'+str(e/'tools'),str(o/'lab_probe.cpp'),str(a.core.resolve()),'-pthread','-o',str(o/'lab_probe')],stdout=f,stderr=f,check=True)
env=os.environ.copy();env['THANDOR_NATIVE_AI_SITE_DUMP']='1'
with (o/'native.log').open('w') as f:subprocess.run([str(a.native_runner.resolve()),'180',str(o/'native'),'12345','mittelpunkt'],cwd=a.game_data.resolve(),env=env,stdout=f,stderr=f,check=True)
lab=o/'lab';env.update(THANDOR_SIMLAB_FACTION_AUDIT_DIR=str(lab),THANDOR_SIMLAB_AUDIT_CPU_SHARE='1',THANDOR_SIMLAB_AUDIT_FACTION_INTERVAL='1',THANDOR_SIMLAB_AUDIT_ENTITY_INTERVAL='1')
with (o/'lab.log').open('w') as f:subprocess.run([str(o/'lab_probe'),str(a.game_data.resolve()),'mittelpunkt','180','1'],env=env,stdout=f,stderr=f,check=True)
def log(path):
 d=collections.defaultdict(list)
 for line in path.read_text().splitlines():
  if line.startswith(('WS05,','WS01,','ACTIVE,')):
   v=line.split(',');d[(v[0],int(v[1]))].append([int(x) for x in v[2:]])
 return d
N,L=log(o/'native.log'),log(o/'lab.log');result={}
for t in [10,18,138,146]:
 x={kind:{'native':N[kind,t],'lab':L[kind,t],'equal':N[kind,t]==L[kind,t]} for kind in ['WS05','WS01','ACTIVE']}
 nsites,lsites=N['WS05',t],L['WS05',t];x['WS05']['equal_points']=[v[:-1] for v in nsites]==[v[:-1] for v in lsites];x['WS05']['uncleared_scores_equal']=len(nsites)==len(lsites) and all(n[-1]==0 or n[-1]==l[-1] for n,l in zip(nsites,lsites));x['WS05']['note']='Native captures after selection, which clears the chosen score to zero; Lab captures before selection.'
 for kind,fmt,bits in [('grid','I',32),('occupancy','Q',64)]:
  n=(o/f'native_{kind}{t}.bin').read_bytes();l=(lab/f'lab_{kind}{t}.bin').read_bytes();assert n[:8]==l[:8];w,h=struct.unpack('<II',n[:8]);ns=struct.unpack('<'+fmt*(w*h),n[8:]);ls=struct.unpack('<'+fmt*(w*h),l[8:]);mask=((1<<bits)-1)^(1 if kind=='grid' else 0);bad=[i for i,(a,b) in enumerate(zip(ns,ls)) if (a^b)&mask];x[kind]={'dimensions':[w,h],'mismatches':len(bad),'examples':[{'x':i%w,'y':i//w,'native':ns[i],'lab':ls[i]} for i in bad[:12]]}
 result[t]=x
(o/'comparison.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({t:{k:v.get('equal',v.get('mismatches')) for k,v in x.items()} for t,x in result.items()},indent=2),flush=True)
