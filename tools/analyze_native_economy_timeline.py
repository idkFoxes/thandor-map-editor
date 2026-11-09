#!/usr/bin/env python3
"""Extract valid FACT changes and model research states; never use legacy ACTR army labels."""
import argparse,json,mmap,struct
from pathlib import Path
from audit_legacy_recording_parity import admitted_frames

def main():
    p=argparse.ArgumentParser();p.add_argument('recording',type=Path);p.add_argument('output',type=Path);p.add_argument('--through-tick',type=int,default=1000);p.add_argument('--model-id',type=int,action='append',default=[]);a=p.parse_args()
    stats={};prior={};models={};changes=[];research=[];commands=[];payload_samples=[];last_tick=None
    with a.recording.open('rb')as stream,mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ)as data:
        for f in admitted_frames(data,stats):
            tick=f['tick'][0]
            if tick>a.through_tick:break
            last_tick=tick
            at=f['offset']
            while True:
                tag,length=struct.unpack_from('<4sI',data,at)
                if tag==b'UCMD':commands.append({'recorded_tick':tick,'words':list(struct.unpack_from('<8I',data,at+8))})
                at+=8+length
                if tag==b'ENDT':break
            for faction,w in f['factions'].items():
                if not 1<=faction<=5:continue
                state={'xenite_q4':w[2],'primary_queue_count':w[14],'secondary_queue_count':w[15],'tech_mask':list(w[50:58])}
                if prior.get(faction)!=state:changes.append({'tick':tick,'faction':faction,**state});prior[faction]=state
            for model,w in f['actors'].items():
                if model in a.model_id:payload_samples.append({'tick':tick,'model_id':model,'offset60':w[26],'offset64':w[27],'offset68':w[28],'model_flags':w[38],'research_technology_id':w[40]})
                state=(w[38]&0xC0,w[40])
                if state!=models.get(model,(0,0)) and (state[0] or models.get(model,(0,0))[0]):research.append({'tick':tick,'model_id':model,'research_flags':state[0],'technology_id':state[1]})
                models[model]=state
            if tick==a.through_tick:break
    result={'format':'V649-admitted-economy-timeline','last_included_tick':last_tick,'partial_scan':True,'admission_read_stats':stats,'excluded_semantics':['ACTR army faction/asset/order attribution','child animation and sound absent from recording'],'fact_changes':changes,'model_research_changes':research,'user_commands':commands,'model_payload_samples':payload_samples}
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('V649_NATIVE_TIMELINE admitted_frames='+str(stats['accepted_frames'])+' last_tick='+str(last_tick)+' fact_changes='+str(len(changes))+' research_changes='+str(len(research)))
if __name__=='__main__':main()
