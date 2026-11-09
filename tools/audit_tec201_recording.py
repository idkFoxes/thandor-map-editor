#!/usr/bin/env python3
"""Classify direct planner arguments in admitted V1 frames; keep bad AIPH fields separate from FACT/ACTR primitives."""
from pathlib import Path
import argparse,hashlib,json,mmap,struct
from audit_legacy_recording_parity import admitted_frames

def analyze(data, through=1100):
    stats={};calls=[];invalid=[];events=[];ticks=[];previous=None
    for frame in admitted_frames(data,stats):
        words=frame['tick'];tick=words[0]
        if tick>through:break
        ticks.append(list(words));at=frame['offset']
        while True:
            tag,size=struct.unpack_from('<4sI',data,at);pos=at+8;at=pos+size
            if tag==b'AIPH':
                w=struct.unpack_from('<4I',data,pos)
                reason=None
                if w[0]!=tick or w[2] != ((tick>>6)&1) or w[3]!=0:reason='inconsistent AIPH metadata'
                elif not 1<=w[1]<=7:reason='out-of-range faction argument; never use as a faction index'
                item={'tick':tick,'words':list(w)}
                if reason:invalid.append({**item,'reason':reason})
                else:calls.append({**item,'passes_tick_slot':w[1]==((tick>>3)&7)})
            if tag==b'ENDT':break
        actor=frame['actors'].get(46)
        if actor is not None:
            value=(actor[38]&0xc0,actor[40])
            if value!=previous:
                events.append({'tick':tick,'flags_c0':value[0],'technology_id':value[1]})
                previous=value
    legacy=[x for x in invalid if x['words'][1]==0x53c811]
    return {'admission':stats,'tick_count':len(ticks),'valid_aiph_count':len(calls),
        'invalid_aiph_count':len(invalid),'legacy_hook_address_plus_one_count':len(legacy),
        'valid_planner_factions':sorted({x['words'][1] for x in calls}),
        'direct_planner_calls':calls,'invalid_planner_arguments':invalid,
        'model46_research_changes':events,'tick_words':ticks,
        'scope':'AIPH fields are direct stack arguments, not the bad ACTR owner lookup. Invalid AIPH arguments are excluded as faction identities. Admitted FACT/root fields remain separate. The ABI diagnosis requires the original caller and hook code, not only this pattern.'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('recording',type=Path);p.add_argument('output',type=Path);p.add_argument('--through-tick',type=int,default=1100);a=p.parse_args()
    with a.recording.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:result=analyze(data,a.through_tick)
    result['input_sha256']=hashlib.sha256(a.recording.read_bytes()).hexdigest()
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ['tick_count','valid_aiph_count','invalid_aiph_count','legacy_hook_address_plus_one_count','valid_planner_factions','model46_research_changes']},indent=2))
if __name__=='__main__':main()
