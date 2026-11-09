#!/usr/bin/env python3
"""Validate complete tick coverage and compare named fields across the two engines."""
import argparse,csv,hashlib,itertools,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('native_prefix',type=Path);p.add_argument('lab_directory',type=Path);p.add_argument('--ticks',type=int,default=36000);p.add_argument('--output',type=Path,default=Path('comparison.json'));p.add_argument('--repeat-prefix',type=Path);a=p.parse_args()
def rows(path):
 with path.open(newline='') as f:
  for r in csv.DictReader(f):
   if any(v is None for v in r.values()):raise ValueError(f'incomplete CSV row: {path}')
   yield {k:int(v) if v.lstrip('-').isdigit() else v for k,v in r.items()}
def native(s):return Path(str(a.native_prefix)+'_'+s+'.csv')
keys=['lifecycle','buildings','units','primary_queue','secondary_queue','xenite_q4','tritium_q4','xenite_capacity_q4','tritium_capacity_q4','energy_capacity_q4','energy_supplied_q4','energy_unpowered_q4','xenite_rate_q4','tritium_rate_q4','rng']+[f'technology{i}' for i in range(8)]
r={'scope':'Every tick: named faction fields and unordered root-army state with positive HP and a linked owner, including dismantling models. Engine hashes are compared only between native repeats. Attached models, internal AI workspaces, all shot events, shadows and pixels are not exhaustively compared. After unmatched spawns, multiset differences can reflect count differences rather than a changed value on a paired entity.','ticks':a.ticks,'faction_fields':keys,'first_faction_difference':{},'mismatch_faction_rows':0,'faction_rows':0,'final':{},'native_repeat':{}}
last=-1
for row in rows(native('ticks')):
 if row['tick']!=last+1:raise ValueError('native tick gap')
 last=row['tick']
if last!=a.ticks:raise ValueError('native ended early')
initial_factions=[v['faction'] for v in itertools.takewhile(lambda v:v['tick']==0,rows(native('factions')))]
if not initial_factions or initial_factions!=list(range(1,len(initial_factions)+1)) or len(initial_factions)>7:raise ValueError('invalid initial faction coverage')
r['active_faction_count']=len(initial_factions)
N=rows(native('factions'));L=rows(a.lab_directory/'factions.csv')
for tick in range(a.ticks+1):
 for f in initial_factions:
  n=next(N);l=next(L)
  if (n['tick'],n['faction'])!=(tick,f) or (l['tick'],l['faction'])!=(tick,f):raise ValueError('faction coverage gap')
  mismatch=False
  for k in keys:
   if n[k]!=l[k]:mismatch=True;r['first_faction_difference'].setdefault(k,{'tick':tick,'faction':f,'native':n[k],'lab':l[k]})
  r['mismatch_faction_rows']+=mismatch;r['faction_rows']+=1
  if tick==a.ticks:r['final'][f]={'native':n,'lab':l}
if next(N,None) is not None or next(L,None) is not None:raise ValueError('extra faction rows')
fields=['faction','arm','class','building','x_q12','y_q12','hp','research_tec','research_elapsed','research_duration','production_arm','production_elapsed','production_duration','z_q12']
r['root_fields']=fields;r['first_root_difference']={};r['root_mismatch_ticks']=0;r['root_ticks']=0
r['joint_root_mismatch_ticks']=0;r['first_joint_root_difference']=None
N=itertools.groupby(rows(native('armies')),lambda r:r['tick']);L=itertools.groupby(rows(a.lab_directory/'entities.csv'),lambda r:r['tick'])
for tick in range(a.ticks+1):
 tn,n=next(N);tl,l=next(L)
 if tn!=tick or tl!=tick:raise ValueError('root tick gap')
 n=[v for v in n if v['faction']>0 and v['hp']>0];l=[v for v in l if v['faction']>0 and v['hp']>0 and not v['removed']]
 for v in n:
  if not v['model_flags']&0xc0:v['research_tec']=v['research_elapsed']=v['research_duration']=0
  if not v['production_active']:v['production_arm']=v['production_elapsed']=v['production_duration']=0
 for v in l:
  if not(v['research_active'] or v['research_pending']):v['research_tec']=v['research_elapsed']=v['research_duration']=0
  if not v['production_arm']:v['production_elapsed']=v['production_duration']=0
 if len(n)!=len(l):r['first_root_difference'].setdefault('count',{'tick':tick,'native':len(n),'lab':len(l)})
 differs=False
 for k in fields:
  ns=sorted((v['faction'],v['arm'],v[k]) for v in n);ls=sorted((v['faction'],v['arm'],v[k]) for v in l)
  if ns!=ls:differs=True;r['first_root_difference'].setdefault(k,{'tick':tick,'native':ns,'lab':ls})
 # Compare complete rows as well as per-field multisets so values cannot
 # silently migrate between two otherwise identical faction/asset owners.
 ns=sorted(tuple(v[k] for k in fields) for v in n);ls=sorted(tuple(v[k] for k in fields) for v in l)
 if ns!=ls:
  differs=True;r['joint_root_mismatch_ticks']+=1
  if r['first_joint_root_difference'] is None:r['first_joint_root_difference']={'tick':tick,'native':ns,'lab':ls}
 r['root_mismatch_ticks']+=differs;r['root_ticks']+=1
 if tick==0:
  ns={(v['faction'],v['arm'],v['x_q12'],v['y_q12']):v for v in n};ls={(v['faction'],v['arm'],v['x_q12'],v['y_q12']):v for v in l}
  if ns.keys()!=ls.keys() or len(ns)!=len(n) or len(ls)!=len(l):raise ValueError('initial faction/asset/XY mismatch')
  r['initial_root_count']=len(n)
  r['initial_identity_xy_hp_match']=all(ns[k]['hp']==ls[k]['hp'] for k in ns)
if next(N,None) is not None or next(L,None) is not None:raise ValueError('extra root ticks')
if a.repeat_prefix:
 for s in ['ticks','factions','armies']:
  h=lambda p:hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
  one,two=h(native(s)),h(Path(str(a.repeat_prefix)+'_'+s+'.csv'));r['native_repeat'][s]={'equal':one==two,'sha256':one,'repeat_sha256':two}
  if one!=two:raise ValueError(f'repeat mismatch {s}')
a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:v for k,v in r.items() if k not in ['final','first_root_difference']},indent=2))
