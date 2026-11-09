from pathlib import Path
from collections import Counter
import json,re
out=[]
for p in sorted(Path('upload').glob('simlab_1792028*.log')):
 rows={};starts=[]
 for line in p.read_text().splitlines():
  kind=line.split(' ',1)[0];d=dict(re.findall(r'(\w+)=([^\s]+)',line))
  if kind=='START':starts.append(d)
  if kind=='RUN': key=(kind,d.get('index'),d.get('tick'))
  elif kind=='ECONOMY':key=(kind,d.get('run'),d.get('tick'),d.get('faction'))
  elif kind=='ENTITY':key=(kind,d.get('run'),d.get('tick'),d.get('entity'))
  else:continue
  rows[key]=d
 runs=[]
 for ri in sorted({k[1] for k in rows if k[0]=='RUN'},key=int):
  rr=[d for k,d in rows.items() if k[0]=='RUN' and k[1]==ri];r=max(rr,key=lambda x:int(x['tick']));tick=int(r['tick'])
  en=[d for k,d in rows.items() if k[0]=='ENTITY' and k[1]==ri and int(k[2])<=tick]
  et=max([int(d['tick']) for d in en],default=0);en=[d for d in en if int(d['tick'])==et]
  ec=[d for k,d in rows.items() if k[0]=='ECONOMY' and k[1]==ri and int(k[2])<=tick]
  ectype=max([int(d['tick']) for d in ec],default=0);ec=[d for d in ec if int(d['tick'])==ectype]
  runs.append({'run':ri,'tick':tick,'run_counters':{k:v for k,v in r.items() if k in ['military_sites','military_anchors','military_groups','military_movement','physical_step_blocks','factory_exit_retries','units_spawned','built']},'entity_tick':et,'arms':dict(Counter(d.get('arm') for d in en)),'factory_states':dict(Counter(d.get('production_state') for d in en if d.get('class')=='13')),'mobile_with_target':sum(d.get('has_move_target')=='1' and int(d.get('speed_q12','0'))>0 for d in en),'economy_tick':ectype,'economy':ec})
 out.append({'file':p.name,'header':p.read_text().splitlines()[0],'starts':starts,'runs':runs})
Path('validation_v612/new_log_analysis.json').write_text(json.dumps(out,indent=2)+'\n')
for f in out:
 print(f['file'], f['header'],f['starts'])
 for r in f['runs']:
  print('RUN',r['run'],r['tick'],r['run_counters'],'ARMS',r['arms'],'FACTORIES',r['factory_states'],'TARGETS',r['mobile_with_target'])
  for e in r['economy']:
   print(' ECON',e['faction'],{k:e.get(k) for k in ['xenite','xenite_cap','research_completed','research_sources','research_positive','research_best','research_score','cache_count','purchase_last_arm','purchase_last_cost','purchase_last_cap','army_ws11']})
