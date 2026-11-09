#!/usr/bin/env python3
"""Compare admitted model-46 research events with explicit legacy-hook and all-AI probes.
Usage: PCK_ROOT INITIAL_WORDS_TSV TGR_PREFIX PROBE_EXE OUTPUT
"""
from pathlib import Path
import hashlib,json,mmap,subprocess,sys
from audit_tec201_recording import analyze

def parse(text):
    transitions=[];seeds={};candidates=[];tick=None;initial=None
    for line in text.splitlines():
        if line.startswith('INITIAL_CHECK '):initial=line
        if line.startswith(('MODEL46 ','RUNTIME ','PURCHASE ','CAND ')):
            fields={k:v for t in line.split()[1:] if '=' in t for k,v in [t.split('=',1)]}
            if line.startswith('MODEL46 '):transitions.append({k:int(v) for k,v in fields.items()})
            elif line.startswith('RUNTIME '):seeds[int(fields['tick'])]=int(fields['seed'])
            elif line.startswith('PURCHASE '):tick=int(fields['tick'])
            elif tick==342:candidates.append({k:int(v) for k,v in fields.items()})
    def event(key):
        return next((t['tick'] for t in transitions if t['tech']==201 and t[key]==1),None)
    assignment=event('assigned');payment=event('active')
    completion=next((t['tick'] for t in transitions if t['tech']==201 and not t['active'] and not t['assigned'] and payment is not None and t['tick']>payment),None)
    return {'assignment':assignment,'payment':payment,'completion':completion,'initial_check':initial,'model46':transitions,'tick342_candidates':candidates},seeds

def main():
    if len(sys.argv)!=6:raise SystemExit(__doc__)
    assets,initial,recording,probe,out=[Path(x).resolve() for x in sys.argv[1:]];out.mkdir(parents=True,exist_ok=True)
    with recording.open('rb')as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)as data:admitted=analyze(data)
    native=admitted['model46_research_changes']
    assignment=next(x['tick'] for x in native if x['technology_id']==201 and x['flags_c0']==128)
    payment=next(x['tick'] for x in native if x['technology_id']==201 and x['flags_c0']==64)
    completion=next(x['tick'] for x in native if x['technology_id']==201 and x['flags_c0']==0 and x['tick']>payment)
    native_events={'assignment':assignment,'payment':payment,'completion':completion};results={}
    native_seeds={w[0]:w[6] for w in admitted['tick_words']}
    for name,extra in [('recorded_hook',['legacy-hook']),('same_bootstrap_all_ai',['bootstrap-only']),('fresh_all_ai',[])]:
        args=[str(probe),str(assets),str(initial),'1000','observed']+extra
        r=subprocess.run(args,capture_output=True,text=True,timeout=120);(out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' probe failed: '+r.stderr[-2000:])
        events,seeds=parse(r.stdout);events['returncode']=r.returncode
        events['matched_native_seed_ticks_through498']=[t for t in range(2,499) if seeds.get(t)==native_seeds.get(t)]
        if name=='recorded_hook':
            assert all(events[k]==v for k,v in native_events.items()),(native_events,events)
            assert all(seeds[t]==native_seeds[t] for t in range(2,90)),'bootstrap held-out prefix differs'
            assert seeds[342]==native_seeds[342]==1835659211
            assert [c['id'] for c in events['tick342_candidates']]==[301,201,321]
            assert events['tick342_candidates'][1]['decision']==2
            assert events['tick342_candidates'][1]['after']==1600
        elif name=='same_bootstrap_all_ai':assert seeds[342]!=native_seeds[342],'ablation did not distinguish recorded RNG trajectory'
        events['tick342_seed']=seeds.get(342)
        events['research_events_match_recording']=all(events[k]==v for k,v in native_events.items())
        results[name]=events
    result={'native_admitted_events':native_events,'comparisons':results,
        'admission':admitted['admission'],'valid_planner_factions':admitted['valid_planner_factions'],
        'excluded_aiph_arguments':admitted['invalid_aiph_count'],
        'bootstrap':'One inverse reconstruction of40 initial emitter RNG calls at the tick2 checkpoint, initial clock1, then no seed injection. Callback changes367/371/375/377 are recovered; other batch intervals assumed.',
        'scope':'Reproduces TEC201 and distinguishes the recorder-corrupted RNG trajectory from all-AI scheduling. Correct owner-before-world-job timing can give both schedules the same research event ticks; those ticks alone do not identify planner dispatch. No full replay/GPU parity claim.'}
    (out/'tec201_comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'native':native_events,'comparisons':{n:{k:r[k] for k in native_events} for n,r in results.items()}},indent=2))
if __name__=='__main__':main()
