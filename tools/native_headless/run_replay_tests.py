#!/usr/bin/env python3
"""Reproduce native repeats and a complete Lab baseline. Does not run comparison implicitly."""
import argparse,csv,hashlib,json,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('game_data',type=Path);p.add_argument('--output',type=Path,default=Path('replay_test'));p.add_argument('--ticks',type=int,default=36000);p.add_argument('--seed',type=int,default=12345);p.add_argument('--map',default='mittelpunkt');p.add_argument('--lab-cpu-share',type=float,default=0.5);a=p.parse_args()
r=Path(__file__).resolve().parent;out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);env=os.environ.copy();env.pop('DISPLAY',None);env.pop('WAYLAND_DISPLAY',None)
h=lambda f:hashlib.file_digest(f.open('rb'),'sha256').hexdigest()
result={'map':a.map,'seed':a.seed,'ticks':a.ticks,'native_callback_step_ticks':1,'game_speed_q8':256,'stage':'simulations_complete_before_comparison','runs':[]}
for n in [1,2]:
 prefix=out/f'run{n}';log=out/f'run{n}.log';begin=time.monotonic()
 with log.open('w') as f:subprocess.run([str(r/'open-thandor-headless'),str(a.ticks),str(prefix),str(a.seed),a.map],cwd=a.game_data.resolve(),env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
 hashes={s:h(Path(str(prefix)+'_'+s+'.csv')) for s in ['ticks','factions','armies']};result['runs'].append({'run':n,'sha256':hashes,'wall_seconds':time.monotonic()-begin})
 with Path(str(prefix)+'_ticks.csv').open() as f:
  rows=list(csv.DictReader(f));assert [int(row['tick']) for row in rows]==list(range(a.ticks+1))
  assert all(rows[0].get('lifecycle'+str(slot))=='0' for slot in [5,6,7]),'Four-faction control has active empty faction slots'
 with Path(str(prefix)+'_factions.csv').open() as f:
  seq=[(int(row['tick']),int(row['faction'])) for row in csv.DictReader(f)];assert seq==[(t,f) for t in range(a.ticks+1) for f in range(1,5)]
 with Path(str(prefix)+'_armies.csv').open() as f:
  initial={}; moved=False
  for row in csv.DictReader(f):
   if int(row['faction'])<=0 or row['arm']!='1':continue
   key=(row['faction'],row['slot']);xy=(row['x_q12'],row['y_q12'])
   if row['tick']=='0':initial[key]=xy
   elif key in initial and initial[key]!=xy:moved=True
  if a.ticks>=600:assert moved,'No initial mobile army moved: check native bootstrap tables'
 print(f'Native run {n} complete; coverage and movement PASS',flush=True)
assert result['runs'][0]['sha256']==result['runs'][1]['sha256'],'Native repeat differs'
lab=out/'lab';env.update({'THANDOR_SIMLAB_FACTION_AUDIT_DIR':str(lab),'THANDOR_SIMLAB_AUDIT_SEED':str(a.seed),'THANDOR_SIMLAB_AUDIT_CPU_SHARE':str(a.lab_cpu_share),'THANDOR_SIMLAB_AUDIT_FACTION_INTERVAL':'1','THANDOR_SIMLAB_AUDIT_ENTITY_INTERVAL':'1'})
with (out/'lab.log').open('w') as f:subprocess.run([str(r/'lab_replay'),str(a.game_data.resolve()),a.map,str(a.ticks),'1'],cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
with (lab/'factions.csv').open() as f:assert [(int(row['tick']),int(row['faction'])) for row in csv.DictReader(f)]==[(t,f) for t in range(a.ticks+1) for f in range(1,5)]
result['lab_sha256']={s:h(lab/s) for s in ['factions.csv','entities.csv','state_hashes.csv']};(out/'checkpoint.json').write_text(json.dumps(result,indent=2)+'\n');print('Both engines complete. Save this checkpoint, then run compare_traces.py separately.',flush=True)
