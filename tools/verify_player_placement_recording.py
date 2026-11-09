#!/usr/bin/env python3
"""Verify the explicitly inferred player-input experiment against admitted roots/facts.
Usage: RECORDING PREVIOUS_V661_TRACE CURRENT_WORLD_TRACE OUTPUT_JSON
Does not run the engine, inject state, or admit ACTR faction/army/command labels.
"""
from pathlib import Path
import hashlib,json,mmap,re,sys
from audit_legacy_recording_parity import admitted_frames
from verify_owner_order_recording import compare,facts

def rows(text,prefix):
    return [{k:int(v)for k,v in re.findall(r'(\w+)=(-?\d+)',line)}
            for line in text.splitlines()if line.startswith(prefix+' ')]

def main():
    if len(sys.argv)!=5:raise SystemExit(__doc__)
    recording,previous,current,out=map(Path,sys.argv[1:]);text=current.read_text()
    observed=facts(text);births={};pioneer={};sample_count=0
    with recording.open('rb')as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)as data:
        before=compare(admitted_frames(data,{}),previous.read_text())
        result=compare(admitted_frames(data,{}),text)
        for frame in admitted_frames(data,{}):
            tick=frame['tick'][0]
            if tick<2:continue
            sample_count+=1
            for faction in (1,2):
                assert observed[tick,faction]['xenite_rate']==frame['factions'][faction][4]
                assert observed[tick,faction]['primary_alert']==frame['factions'][faction][24]
            if 437 in frame['actors']:pioneer[tick]=frame['actors'][437]
            for identity,w in frame['actors'].items():
                if identity>441 and identity not in births:
                    births[identity]={'tick':tick,'x_q12':w[4]if w[4]<2**31 else w[4]-2**32,
                      'y_q12':w[5]if w[5]<2**31 else w[5]-2**32,'yaw':w[9],'health':w[19]}
    assert before['rng']['first_mismatch']==799
    assert result['rng']['first_mismatch']is None and result['rng']['matched']==sample_count==1099
    assert result['TEC201']=={'assignment':342,'payment':453,'completion':498}
    for faction in ('1','2'):
        assert all(v['matched']==v['compared']==sample_count for v in result['faction_fields'][faction].values())
    created=rows(text,'CREATED');matches=[]
    for identity,tick,faction,army in [(442,650,1,330),(444,722,2,301),(445,1026,1,301)]:
        candidate=next(v for v in created if v['tick']==tick and v['faction']==faction and v['arm']==army)
        native=births[identity]
        assert all(candidate[k]==native[k]for k in ('tick','x_q12','y_q12','yaw','health'))
        matches.append({'native_model':identity,'native':native,'lab':candidate,
          'identity_policy':'source/outcome-supported counterfactual; ACTR labels excluded'})
    conversion=next(v for v in created if v['arm']==390 and v['faction']==1)
    assert all(conversion[k]==births[443][k]for k in ('tick','x_q12','y_q12','health'))
    commands=rows(text,'PLAYER_RESULT');assert len(commands)==5 and all(v['accepted']==1 for v in commands)
    samples=[v for v in rows(text,'HUMAN_MODEL')if v['model']==437 and v['arm']==50]
    motion_mismatch=[];heading_mismatch=[]
    for sample in samples:
        w=pioneer.get(sample['tick'])
        if w is None:continue
        signed=lambda n:n if n<2**31 else n-2**32
        if (sample['x_q12'],sample['y_q12'])!=(signed(w[4]),signed(w[5])):motion_mismatch.append(sample['tick'])
        if sample['yaw']!=w[9]:heading_mismatch.append(sample['tick'])
    output={'previous':before,'explicit_inferred_world':result,'factory_and_extractor_matches':matches,
      'conversion':{'lab':conversion,'native':births[443],'heading_matches':conversion['yaw']==births[443]['yaw']},
      'player_commands':commands,'extra_fact_samples_per_faction':sample_count,
      'remaining_pioneer_pose_gap':{'xy_mismatching_samples':len(motion_mismatch),'first_xy_mismatch':next(iter(motion_mismatch),None),
        'heading_mismatching_samples':len(heading_mismatch),'first_heading_mismatch':next(iter(heading_mismatch),None)},
      'recording_sha256':hashlib.sha256(recording.read_bytes()).hexdigest(),
      'scope':'1099 RNG and selected faction1/2 fact samples, three placement roots and conversion time/XY/health. Explicit inferred inputs and legacy recorder-corruption mode; not recovered commands or complete world/movement parity.'}
    out.write_text(json.dumps(output,indent=2)+'\n')
    print(json.dumps({'rng':result['rng'],'TEC201':result['TEC201'],'faction1_and_2_selected_facts_match':True,
      'placement_root_matches':len(matches),'conversion_tick':conversion['tick'],
      'remaining_pioneer_pose_gap':output['remaining_pioneer_pose_gap']},indent=2))
if __name__=='__main__':main()
