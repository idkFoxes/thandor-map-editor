#!/usr/bin/env python3
"""Compare command-only movement with the admitted legacy prefix. Never injects controls.
Usage: RECORDING PREVIOUS_INFERRED_TRACE CURRENT_DIRECT_TRACE CURRENT_INFERRED_TRACE OUTPUT
"""
from pathlib import Path
import json,mmap,re,sys
from audit_legacy_recording_parity import admitted_frames
from verify_owner_order_recording import compare,facts

def main():
    if len(sys.argv)!=6:raise SystemExit(__doc__)
    recording,previous,direct,inferred,out=map(Path,sys.argv[1:]);results={}
    births=[];prior_ids=set();native_alerts={}
    with recording.open('rb')as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)as data:
        for label,path in [('previous',previous),('command_only_direct',direct),('command_only_inferred',inferred)]:
            results[label]=compare(admitted_frames(data,{}),path.read_text())
        for frame in admitted_frames(data,{}):
            if frame['tick'][0]>=2:native_alerts[frame['tick'][0]]=frame['factions'][2][24]
            for identity,w in frame['actors'].items():
                if identity not in prior_ids and identity>441:
                    births.append({'tick':frame['tick'][0],'model':identity,'x_q12':w[4]if w[4]<2**31 else w[4]-2**32,'y_q12':w[5]if w[5]<2**31 else w[5]-2**32,'yaw':w[9],'health':w[19]})
            prior_ids.update(frame['actors'])
    assert results['previous']['rng']['first_mismatch']==722
    assert results['command_only_direct']['rng']['first_mismatch']==477
    current=results['command_only_inferred'];assert current['rng']['first_mismatch']==799
    for label,result in results.items():assert result['TEC201']=={'assignment':342,'payment':453,'completion':498},label
    assert all(v['matched']==v['compared']==1099 for v in current['faction_fields']['2'].values())
    current_facts=facts(inferred.read_text())
    assert len(native_alerts)==1099 and all(current_facts[t,2]['primary_alert']==value==0 for t,value in native_alerts.items())
    created=[]
    for line in inferred.read_text().splitlines():
        if line.startswith('CREATED '):created.append({k:int(v)for k,v in re.findall(r'(\w+)=(-?\d+)',line)})
    candidate=next(v for v in created if v['tick']==722 and v['faction']==2 and v['arm']==301)
    native=next(v for v in births if v['tick']==722)
    assert all(candidate[k]==native[k]for k in ['tick','x_q12','y_q12','yaw','health'])
    result={'comparisons':results,'creation_match':{'lab':candidate,'native':native},'primary_alert_matches':len(native_alerts),'scope':'Source-correct movement; same explicit inferred queue controls and recorded planner corruption. Faction2 selected facts and first factory match; missing human controls and full world-state parity remain unresolved.'}
    out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'rng_first_mismatch':{k:v['rng']['first_mismatch']for k,v in results.items()},'TEC201':current['TEC201'],'faction2_all_selected_facts_match':True,'factory_pose_match':candidate},indent=2))
if __name__=='__main__':main()
