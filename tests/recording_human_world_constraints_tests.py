from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_human_world_constraints import analyze,source_metadata

def root(x=100,health=1000):
    w=[0]*41;w[1]=7;w[2]=999;w[4:7]=[x,200,300];w[9]=400;w[19]=health
    return tuple(w)
def frame(t,roots,count=1,human=2):
    fact=[0]*74;fact[14]=count
    tick=[0]*10;tick[0]=t;tick[9]=human
    return {'tick':tick,'actors':roots,'factions':{1:fact}}
metadata='''INITIAL_MAP checked=441 exact_xy=441 reversed=1
INITIAL_MODEL id=10 arm=50 faction=1 class=18
CATALOG_BUILD arm=50 cost=1 duration=1 root_hp=1000 class_c0=390
CATALOG_BUILD arm=390 cost=0 duration=0 root_hp=8000 class_c0=0
'''
models,assets=source_metadata(metadata)
frames=[frame(2,{10:root()}),frame(3,{10:root(105)}),frame(4,{11:root(105,8000)},0)]
r=analyze(frames,models,assets)
assert r['human_motion_constraints'][0]['first_motion_tick']==3
turned=list(root());turned[9]+=1
heading=analyze([frames[0],frame(3,{10:tuple(turned)})],models,assets)
assert not heading['human_motion_constraints'] and heading['human_heading_constraints'][0]['first_heading_tick']==3
assert r['source_correlated_conversions'][0]['new_arm_from_source']==390
assert r['source_correlated_conversions'][0]['faction_from_source']==1 # corrupted ACTR labels excluded
assert r['human_primary_stock_constraints'][0]['tick']==4
assert not analyze([frames[0],frame(3,{10:root(105)},human=0)],models,assets)['human_motion_constraints']
assert not analyze([frames[1],frame(4,{11:root(105,8000),12:root(105,8000)},0)],models,assets)['source_correlated_conversions']
assert not analyze([frames[1],frame(5,{11:root(105,8000)},0)],models,assets)['source_correlated_conversions']
try:source_metadata(metadata.replace('reversed=1','reversed=0'))
except ValueError:pass
else:raise AssertionError('unverified initial identity admitted')
print('HUMAN_WORLD_CONSTRAINTS PASS labels_excluded=1 ambiguous_conversion_excluded=1 gaps_excluded=1')
