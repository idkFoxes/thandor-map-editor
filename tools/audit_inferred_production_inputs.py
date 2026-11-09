#!/usr/bin/env python3
"""Bound inferred production inputs using admitted root fields and source-resolved identities.
No ACTR faction/army labels are admitted. These are constraints from outcomes,
not recovered UCMD packets, and never become automatic engine inputs.
"""
import argparse,hashlib,json,mmap,re,struct
from pathlib import Path
from audit_legacy_recording_parity import admitted_frames

def parse_source_metadata(text):
    models={};products={}
    for line in text.splitlines():
        values={k:int(v)for k,v in re.findall(r'(\w+)=(\d+)',line)}
        if line.startswith('INITIAL_MODEL '):models[values['id']]={'faction':values['faction'],'class':values['class'],'asset':values['arm']}
        if line.startswith('CATALOG_BUILD '):products[values['arm']]={'cost':values['cost'],'duration':values['duration']}
    if not models or not products or 'reversed=1'not in text:
        raise ValueError('source-resolved initial map identity and product metadata required')
    return models,products

def analyze(frames,models,products):
    previous=None;events=[];excluded=[];batch_samples={};conflicts=[]
    for frame in frames:
        tick=frame['tick'][0]
        if previous is not None and previous['tick'][0]+1==tick:
            prior=previous['actors']
            for model,w in frame['actors'].items():
                old=prior.get(model);identity=models.get(model)
                if old is None or identity is None or identity['class']not in(11,13,22):continue
                # Native factory classLinkState: +60 ARM, +64 elapsed, +68 duration;
                # +EC producing bit100. These are ModelRuntimeSlot words, not army labels.
                if(w[38]&0x100)and(old[38]&0x100)and w[26]==old[26]and w[28]==old[28]:
                    delta=(w[27]-old[27])&0xffffffff
                    if 1<=delta<=5:
                        if tick in batch_samples and batch_samples[tick]!=delta:conflicts.append({'tick':tick,'model':model,'delta':delta,'other':batch_samples[tick]})
                        else:batch_samples[tick]=delta
                if not(w[38]&0x100)or old[38]&0x100 or w[27]!=0 or w[28]==0:continue
                faction=identity['faction']
                if not(frame['tick'][9]&(1<<faction)):continue
                product=products.get(w[26]);before=previous['factions'].get(faction);after=frame['factions'].get(faction)
                reason=None
                if product is None:reason='product cost/duration unresolved in supplied source metadata'
                elif product['duration']!=w[28]:reason='observed duration differs from source product'
                elif before is None or after is None:reason='missing adjacent faction facts'
                elif before[2]-after[2]!=product['cost']:reason='balance delta does not uniquely corroborate product cost'
                row={'tick':tick,'model_id':model,'faction_from_source':faction,'asset_from_model_slot':w[26],'duration':w[28]}
                if reason:excluded.append({**row,'reason':reason});continue
                events.append({**row,'cost_q4':product['cost'],'balance_before':before[2],'balance_after':after[2],
                    'classification':'inferred pre-owner queue admission; not a captured UCMD command',
                    'evidence':'producing bit rises with ARM/duration/zero elapsed; source-resolved human factory identity; exact FACT balance deduction'})
        previous=frame
    conflict_ticks={row['tick']for row in conflicts}
    for tick in conflict_ticks:batch_samples.pop(tick,None)
    intervals=[]
    for tick,batch in sorted(batch_samples.items()):
        if intervals and intervals[-1]['through_tick']+1==tick and intervals[-1]['batch']==batch:intervals[-1]['through_tick']=tick
        else:intervals.append({'from_tick':tick,'through_tick':tick,'batch':batch})
    return {'inferred_queue_inputs':events,'excluded_candidates':excluded,'observed_callback_batch_intervals':intervals,'conflicting_batch_samples':conflicts,
        'admitted_semantics':['root ModelRuntimeSlot +60/+64/+68/+EC','FACT balances','TICK human mask','source-resolved initial LEV identity'],
        'excluded_semantics':['legacy ACTR army faction/asset/command labels','exact queue command packet or wall time','callback batches outside observed counter intervals']}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('recording',type=Path);ap.add_argument('source_probe_log',type=Path);ap.add_argument('output_json',type=Path);ap.add_argument('output_tsv',type=Path);ap.add_argument('--through-tick',type=int,default=1100);args=ap.parse_args()
    models,products=parse_source_metadata(args.source_probe_log.read_text());stats={};commands=0
    with args.recording.open('rb')as file,mmap.mmap(file.fileno(),0,access=mmap.ACCESS_READ)as data:
        def scan():
            nonlocal commands
            for frame in admitted_frames(data,stats):
                if frame['tick'][0]>args.through_tick:break
                at=frame['offset']
                while True:
                    tag,length=struct.unpack_from('<4sI',data,at);at+=8+length
                    commands+=tag==b'UCMD'
                    if tag==b'ENDT':break
                yield frame
        result=analyze(scan(),models,products)
    result.update(admission=stats,captured_ucmd_count=commands,source_probe_sha256=hashlib.sha256(args.source_probe_log.read_bytes()).hexdigest(),recording_sha256=hashlib.sha256(args.recording.read_bytes()).hexdigest())
    args.output_json.write_text(json.dumps(result,indent=2)+'\n')
    args.output_tsv.write_text(''.join(f"{event['tick']}\t{event['faction_from_source']}\t{event['asset_from_model_slot']}\n"for event in result['inferred_queue_inputs']))
    print(json.dumps({'inferred_inputs':result['inferred_queue_inputs'],'captured_ucmd_count':commands,'batch_intervals':result['observed_callback_batch_intervals']},indent=2))
if __name__=='__main__':main()
