#!/usr/bin/env python3
"""Compare admitted TICK primary seeds with V652 probe snapshots.
Usage: ADMITTED_EVIDENCE.json LAB_LOG OUTPUT.json
Distances assume one continuous primary stream with no seed resets. They are
minimal forward Random_Next transitions modulo its 2**31-call cycle, not proof
of which branch consumed a draw. No malformed V1 ACTR army labels are used.
"""
import json,random,re,sys
from pathlib import Path
MASK=(1<<32)-1
PERIOD=1<<31
def step(seed):
    first=(seed*33+101)&MASK
    return (first*33+101)&MASK
def distance(start,target):
    if (start^target)&1:
        raise ValueError('Seeds are on different parity cycles; reset or non-native transition required')
    current=start;mul=1089;add=3434;count=0
    # One call advances two LCG steps. Bit 0 is invariant; the induced LCG
    # on the remaining 31 bits has odd increment and multiplier 1 mod 4.
    for bit in range(31):
        if (current^target)&(1<<(bit+1)):
            current=(current*mul+add)&MASK
            count|=1<<bit
        mul,add=(mul*mul)&MASK,(add*(mul+1))&MASK
    if current!=target:raise AssertionError('Unresolved seed distance')
    return count
def verify():
    rng=random.Random(0x652);checks=0
    for _ in range(64):
        start=rng.randrange(1<<32);state=start
        for n in range(257):
            assert distance(start,state)==n
            state=step(state);checks+=1
    try:distance(0,1)
    except ValueError:checks+=1
    else:raise AssertionError('Parity mismatch accepted')
    return checks
def main():
    if len(sys.argv)!=4:raise SystemExit(__doc__)
    evidence,lab,output=map(Path,sys.argv[1:]);checks=verify()
    frames=json.loads(evidence.read_text())['sampled_frames']
    native={f['tick_words'][0]:f['tick_words'][6] for f in frames}
    snapshots={int(t):int(s) for t,s in re.findall(r'^FRAME tick=(\d+) rng=(\d+) ',lab.read_text(),re.M)}
    rows=[];previous=None
    for tick,seed in sorted(native.items()):
        row={'tick':tick,'native_primary_seed':seed}
        if previous:row['native_minimum_calls_since_previous_sample']=distance(previous[1],seed)
        if tick in snapshots:
            value=snapshots[tick];row['lab_primary_seed']=value;row['equal']=value==seed
            row['lab_to_native_minimum_forward_calls']=distance(value,seed)
            if previous and previous[0] in snapshots:
                row['lab_minimum_calls_since_previous_sample']=distance(snapshots[previous[0]],value)
        rows.append(row);previous=(tick,seed)
    result={'scope':__doc__.strip(),'self_check_forward_cases':checks,'period_calls':PERIOD,'samples':rows}
    output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
