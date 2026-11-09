#!/usr/bin/env python3
"""Audit V1 capture semantics using supplied recorder source and retail layouts.

V1 ACTR's pointer argument is ModelRuntimeSlot, not ArmyRuntimeSlot. Preserve
that distinction: FACT/TICK and node transforms are valid, while army ownership,
asset IDs, commands and AI attribution are not. No model hierarchy/audio capture.
"""
import argparse
from collections import Counter
import json
import mmap
from pathlib import Path
import struct

TICK_FIELDS = ('simulationTick presentationTick periodicClockTick gameSpeedQ8 '
               'relationUiFlags networkTick primarySeed secondarySeed rngSelector '
               'humanFactionMask playerCount pendingOverflowCount').split()


def added_bits(before, after):
    return [32*i+b for i, (a, z) in enumerate(zip(before, after))
            for b in range(32) if (z & ~a) & (1 << b)]


def analyze(path):
    out = {'format': 'THDTGR1-v646-schema-audit', 'errors': [], 'limitations': [
        'V1 ACTR was captured from a ModelRuntimeSlot pointer using an ArmyRuntimeSlot layout.',
        'V1 factionIndex is attachmentCount; armyAssetId is unresolved model state at +0xA0.',
        'V1 actor command/target/movement fields and derived AIAA/RMOV faction attribution are invalid army evidence.',
        'Root node poses and the recovered model health/flags/research fields remain inspectable.',
        'No child poses, walker contacts, track offsets or audio requests were captured.',
        'No deterministic editor/native replay or rendered/aural parity claim.',
    ]}
    previous_factions, research_changes = {}, []
    actors, first_tick, last_tick = {}, None, None
    attachment_counts, asset_words, research_counts = Counter(), Counter(), Counter()
    overflow, pose_changes, health_decreases = 0, 0, 0
    peak_researching = 0
    ticks = 0
    with path.open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        if len(data)<192 or data[:8]!=b'THDTGR1\0':
            out['errors'].append('bad header'); return out
        version, header = struct.unpack_from('<II', data, 8)
        if version!=1 or header!=192:
            out['errors'].append('semantic recovery is restricted to supplied V1 recorder'); return out
        at=192
        while at<len(data):
            if at+8>len(data): out['errors'].append('truncated header'); break
            tag=data[at:at+4]; n=struct.unpack_from('<I',data,at+4)[0]; p=at+8
            if n>16*1024*1024 or p+n>len(data): out['errors'].append('bad payload bounds'); break
            expected={b'TICK':48,b'FACT':296,b'ACTR':164,b'ENDT':16}.get(tag)
            if expected is not None and n!=expected: out['errors'].append('bad payload size'); break
            if tag==b'TICK':
                tick=struct.unpack_from('<12I',data,p);ticks+=1
                first_tick=first_tick or dict(zip(TICK_FIELDS,tick));last_tick=dict(zip(TICK_FIELDS,tick));overflow+=tick[11]
                researching=0
            elif tag==b'FACT':
                w=struct.unpack_from('<74I',data,p); idx=w[0]; mask=w[50:58]
                if idx>=8:out['errors'].append('invalid faction index');break
                if idx in previous_factions:
                    old=previous_factions[idx]
                    added=added_bits(old,mask);removed=added_bits(mask,old)
                    if added or removed:research_changes.append({'simulation_tick':last_tick['simulationTick'],'faction':idx,'added_technology_ids':added,'removed_technology_ids':removed})
                previous_factions[idx]=mask
            elif tag==b'ACTR':
                w=struct.unpack_from('<41I',data,p);key=w[0];pose=w[4:11];health=w[19]
                attachment_counts[w[1]]+=1;asset_words[w[2]]+=1
                if key in actors:
                    a=actors[key];changed=a['last_pose']!=pose;hurt=health<a['last_health']
                    pose_changes+=changed;health_decreases+=hurt
                    a['pose_changed']|=changed;a['health_decreased']|=hurt
                    a['minimum_health']=min(a['minimum_health'],health)
                    a.update(last_pose=pose,last_health=health,last_tick=last_tick['simulationTick'])
                else:actors[key]={'first_tick':last_tick['simulationTick'],'last_tick':last_tick['simulationTick'],'last_pose':pose,'last_health':health,'minimum_health':health,'pose_changed':False,'health_decreased':False}
                # Actual model +0xEC flags and +0x100 researchTechnologyId.
                if w[38]&0xc0:
                    researching+=1;research_counts[w[40]]+=1
            elif tag==b'ENDT':peak_researching=max(peak_researching,researching)
            at=p+n
    out.update(ticks=ticks,first_tick=first_tick,last_tick=last_tick,pending_overflow=overflow,
        distinct_recorded_root_models=len(actors),attachment_count_distribution=dict(attachment_counts),
        mislabelled_asset_word_distribution=dict(asset_words),
        root_models_with_pose_changes=sum(a['pose_changed'] for a in actors.values()),
        root_models_with_health_decreases=sum(a['health_decreased'] for a in actors.values()),
        root_models_reaching_zero_health=sum(a['minimum_health']==0 for a in actors.values()),
        pose_change_samples=pose_changes,health_decrease_samples=health_decreases,
        research_mask_transitions=research_changes,peak_simultaneously_researching_roots=peak_researching,
        researching_root_samples_by_technology=dict(research_counts),
        final_technology_ids_by_faction={str(i):added_bits((0,)*8,m) for i,m in previous_factions.items()})
    return out


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('recording',type=Path);p.add_argument('output',type=Path);a=p.parse_args();r=analyze(a.recording)
    a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:r.get(k) for k in ('ticks','distinct_recorded_root_models','root_models_with_pose_changes','root_models_with_health_decreases','root_models_reaching_zero_health','peak_simultaneously_researching_roots','pending_overflow','errors')}))
    print('research mask transitions',len(r.get('research_mask_transitions',[])))
    return bool(r['errors'])
if __name__=='__main__':raise SystemExit(main())
