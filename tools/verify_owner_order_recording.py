#!/usr/bin/env python3
"""Compare source-correct owner ordering and explicit inferred-input ablations with admitted frames."""
import argparse,json,mmap,re
from pathlib import Path
from audit_legacy_recording_parity import admitted_frames
from verify_tec201_timeline import parse
FIELDS={'xenite':2,'tritium':6,'primary':14,'secondary':15,**{f'tech{i}':50+i for i in range(8)}}
def facts(text):
    result={}
    for line in text.splitlines():
        if not line.startswith('RUNTIME_FACT '):continue
        w={k:int(v)for k,v in re.findall(r'(\w+)=(\d+)',line)}
        result[w['tick'],w['faction']]=w
    return result

def compare(frames,text):
    events,seeds=parse(text);observed=facts(text);bad=[];matched=0;stats={}
    for frame in frames:
        tick=frame['tick'][0]
        if tick<2 or tick not in seeds:continue
        if seeds[tick]==frame['tick'][6]:matched+=1
        else:bad.append(tick)
        for faction,w in frame['factions'].items():
            row=observed.get((tick,faction))
            if row is None:continue
            for name,index in FIELDS.items():
                item=stats.setdefault((faction,name),{'compared':0,'matched':0,'first_mismatch':None})
                item['compared']+=1
                if row[name]==w[index]:item['matched']+=1
                elif item['first_mismatch']is None:item['first_mismatch']={'tick':tick,'native':w[index],'lab':row[name]}
    return {'rng':{'matched':matched,'compared':matched+len(bad),'first_mismatch':bad[0]if bad else None,'mismatches':bad},
            'TEC201':{k:events[k]for k in ('assignment','payment','completion')},
            'faction_fields':{str(f):{name:v for (owner,name),v in stats.items()if owner==f}for f in sorted({f for f,_ in stats})}}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('recording',type=Path);ap.add_argument('baseline_log',type=Path);ap.add_argument('merged_log',type=Path);ap.add_argument('inferred_input_log',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args();results={}
    with a.recording.open('rb')as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)as data:
        for label,path in [('previous',a.baseline_log),('merged_owner_order',a.merged_log),('explicit_inferred_inputs',a.inferred_input_log)]:
            results[label]=compare(admitted_frames(data,{}),path.read_text())
    assert results['previous']['rng']['first_mismatch']==384
    assert results['merged_owner_order']['rng']['first_mismatch']==477
    assert results['explicit_inferred_inputs']['rng']['first_mismatch']==722
    for result in results.values():assert result['TEC201']=={'assignment':342,'payment':453,'completion':498}
    output={'comparisons':results,'scope':'Original bootstrap and recorder-corrupted planner mask. Additional queue inputs are inferred controls, not captured UCMD packets. RNG agreement does not imply all world/economy fields agree.'}
    a.output.write_text(json.dumps(output,indent=2)+'\n')
    print(json.dumps({k:{'rng_first_mismatch':v['rng']['first_mismatch'],'rng_matched':v['rng']['matched'],'TEC201':v['TEC201']}for k,v in results.items()},indent=2))
if __name__=='__main__':main()
