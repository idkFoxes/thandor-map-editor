#!/usr/bin/env python3
"""Extract bounded human-world constraints from admitted model roots and source metadata.
Usage: RECORDING SOURCE_PROBE_LOG OUTPUT_JSON
No ACTR army/faction/command labels or inferred controls are applied to the engine.
"""
from pathlib import Path
import hashlib,json,mmap,re,sys
from audit_legacy_recording_parity import admitted_frames

def source_metadata(text):
    models={};assets={}
    if 'INITIAL_MAP checked=441 exact_xy=441 reversed=1'not in text:raise ValueError('verified source/map identity required')
    for line in text.splitlines():
        w={k:int(v)for k,v in re.findall(r'(\w+)=(-?\d+)',line)}
        if line.startswith('INITIAL_MODEL '):models[w['id']]=w
        elif line.startswith('CATALOG_BUILD '):assets[w['arm']]=w
    if not models or any('class_c0'not in a or 'root_hp'not in a for a in assets.values()):raise ValueError('complete source model metadata required')
    return models,assets

def analyze(frames,models,assets):
    previous=None;seen=set();motion={};heading={};conversions=[];staging=[];births=[]
    for frame in frames:
        tick=frame['tick'][0];roots=frame['actors'];human=frame['tick'][9]
        born=set(roots)-seen;seen.update(roots)
        if previous and previous['tick'][0]+1==tick:
            old=previous['actors']
            for identity,meta in models.items():
                faction=meta['faction']
                if meta['class']!=18 or not(human&(1<<faction)) or identity not in old:continue
                before=old[identity];now=roots.get(identity)
                if now and now[9]!=before[9]:
                    row=heading.setdefault(identity,{'model':identity,'faction_from_source':faction,'first_heading_tick':tick,'last_heading_tick':tick,'turning_updates':0})
                    row['last_heading_tick']=tick;row['turning_updates']+=1
                if now and now[4:6]!=before[4:6]:
                    row=motion.setdefault(identity,{'model':identity,'faction_from_source':faction,'first_motion_tick':tick,'last_motion_tick':tick,'moving_updates':0})
                    row['last_motion_tick']=tick;row['moving_updates']+=1
                if now is not None:continue
                source=assets.get(meta['arm'],{});target=assets.get(source.get('class_c0'),{})
                if source.get('root_hp',0)<=0 or target.get('root_hp',0)<=0:continue
                hp=before[19]*target['root_hp']//source['root_hp']
                matches=[n for n in born if roots[n][4:6]==before[4:6] and roots[n][9]==before[9] and roots[n][19]==hp]
                if len(matches)==1:
                    conversions.append({'tick':tick,'old_model':identity,'new_model':matches[0],'faction_from_source':faction,'old_arm_from_source':meta['arm'],'new_arm_from_source':target['arm'],'position_words':list(before[4:6]),'yaw':before[9],'expected_health':hp,'classification':'source-correlated conversion outcome; not a captured command'})
            for faction,now in frame['factions'].items():
                before=previous['factions'].get(faction)
                if before and human&(1<<faction) and before[14]>now[14]:
                    staging.append({'tick':tick,'faction':faction,'count_before':before[14],'count_after':now[14],'classification':'primary-stock removal constraint; placement/staging command not captured'})
        for identity in sorted(born-set(models)):
            w=roots[identity]
            births.append({'tick':tick,'model':identity,'position_words':list(w[4:7]),'yaw':w[9],'health':w[19],'compatible_baseline_health_assets':[a['arm']for a in assets.values()if a.get('root_hp')==w[19]],'faction_and_army_labels_admitted':False})
        previous=frame
    return {'human_motion_constraints':list(motion.values()),'human_heading_constraints':list(heading.values()),'source_correlated_conversions':conversions,'human_primary_stock_constraints':staging,'unattributed_model_births':births,'excluded_semantics':['ACTR faction/asset/command labels','exact human command code, waypoint, target or generation','automatic control injection']}

def main():
    if len(sys.argv)!=4:raise SystemExit(__doc__)
    recording,metadata,out=map(Path,sys.argv[1:]);models,assets=source_metadata(metadata.read_text());stats={}
    with recording.open('rb')as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)as data:
        result=analyze(admitted_frames(data,stats),models,assets)
    result.update(admission=stats,recording_sha256=hashlib.sha256(recording.read_bytes()).hexdigest(),source_probe_sha256=hashlib.sha256(metadata.read_bytes()).hexdigest())
    out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k]for k in ['human_motion_constraints','human_heading_constraints','source_correlated_conversions','human_primary_stock_constraints']},indent=2))
if __name__=='__main__':main()
