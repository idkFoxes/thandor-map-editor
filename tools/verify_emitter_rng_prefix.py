#!/usr/bin/env python3
"""Compare current and previous forensic probe logs against admitted recording words.
Usage: NATIVE_TICK_WORDS_JSON CURRENT_PROBE_LOG PREVIOUS_PROBE_LOG OUTPUT_JSON
This does not inject RNG state or infer unrecorded callback batch controls.
"""
import json,sys
from pathlib import Path
from verify_tec201_timeline import parse
if len(sys.argv)!=5:raise SystemExit(__doc__)
native_path,current_path,previous_path,out=map(Path,sys.argv[1:])
native={w[0]:w[6]for w in json.loads(native_path.read_text())['tick_words']}
rows={}
for label,path in [('previous',previous_path),('current',current_path)]:
 events,seeds=parse(path.read_text());end=min(498,max(seeds),max(native))
 mismatches=[t for t in range(2,end+1)if seeds.get(t)!=native.get(t)]
 rows[label]={'first_mismatch':mismatches[0]if mismatches else None,'matched_through498':end-1-len(mismatches),'mismatches_through498':mismatches,'TEC201':{k:events[k]for k in ['assignment','payment','completion']}}
assert rows['previous']['first_mismatch']==167,rows
assert rows['current']['first_mismatch']==384,rows
assert rows['current']['TEC201']=={'assignment':342,'payment':453,'completion':498},rows
result={'comparisons':rows,'held_out_prefix':'Current seeds match every admitted tick2..383, with the single original bootstrap reconstruction and no later seed injection.','remaining_gap':'First mismatch384. Static/live owner interleaving, unobserved callback batch controls and later runtime callbacks remain outside full replay parity.'}
out.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:{a:b for a,b in v.items()if a!='mismatches_through498'}for k,v in rows.items()},indent=2))
