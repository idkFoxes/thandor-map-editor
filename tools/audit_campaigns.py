#!/usr/bin/env python3
"""Audit native CGN graphs and LEV condition/outcome references.
Run thandor_campaign_inventory GAME_ROOT EXTRACTED_DIR > inventory.tsv first.
Usage: audit_campaigns.py EXTRACTED_DIR inventory.tsv OUTPUT_DIRECTORY
No game assets are modified. Structural reachability does not prove a condition can be won.
"""
from pathlib import Path
import collections,csv,hashlib,json,struct,sys

def i32(b,o):return struct.unpack_from('<i',b,o)[0]
def u32(b,o):return struct.unpack_from('<I',b,o)[0]
def text(b,o,size):
    raw=b[o:o+size]
    end=next((i for i in range(0,len(raw)-1,2)if raw[i:i+2]==b'\0\0'),len(raw))
    return raw[:end].decode('utf-16le')
def normal(p):return p.replace('\\','/').lower()
def extension(p,suffix):
    p=normal(p);stem,sep,leaf=p.rpartition('/')
    leaf=leaf.rsplit('.',1)[0] if '.' in leaf else leaf
    return (stem+sep if sep else '')+leaf+suffix

def catalog(path):
    b=path.read_bytes()
    if len(b)%256:raise ValueError(f'{path}: partial catalog record')
    return [dict(identifier=text(b,o,64),title_index=u32(b,o+0x50)) for o in range(0,len(b),256)]

def reachable(first,records,trigger_only):
    by_id={r['id']:r for r in reversed(records)};visited=set();pending=[first]
    while pending:
        node=pending.pop()
        if node in visited:continue
        visited.add(node);record=by_id.get(node)
        if not record:continue
        outcomes={t['outcome'] for t in record.get('triggers',[]) if t['state']==1} if trigger_only else range(8)
        for outcome in outcomes:
            if outcome>=8:continue
            target=record['successors'][outcome]
            if target>=0 and target in by_id and target not in visited:pending.append(target)
    return visited

def main():
    extracted,inventory,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
    names=set();titles={};entries=[]
    for line in inventory.read_text().splitlines():
        row=line.split('\t')
        if row[0]=='ENTRY':
            entry=dict(archive=row[1],path=row[2],original_path=row[3],decoded_bytes=int(row[4]));entries.append(entry);names.add(row[2])
        elif row[0]=='TITLE':titles[int(row[1])]=json.loads(row[2])
    listed=[]
    for p in [extracted/'level/campagne.dat',*sorted((extracted/'level').glob('campagne[0-9][0-9].dat'))]:
        if not p.exists():continue
        for row in catalog(p):
            key=normal(row['identifier']);previous=next((i for i,r in enumerate(listed) if normal(r['identifier'])==key),None)
            if previous is None:listed.append(row)
            else:listed[previous]=row
    listing={normal(r['identifier']).rstrip('.')+'.cgn':r for r in listed}
    campaigns=[]
    for p in sorted(extracted.rglob('*.cgn')):
        b=p.read_bytes();count=i32(b,0xB8)
        if b[:4]!=b'cgn\0' or count<1 or 512+count*384>len(b):raise ValueError(f'{p}: invalid CGN image')
        name=p.relative_to(extracted/'level').as_posix();meta=listing.get(name)
        campaign=dict(path='level/'+name,listed=meta is not None,title=titles.get(meta['title_index']) if meta else None,
                      first_level_id=i32(b,0xB4),saved_current_level_id=i32(b,0xC4),record_count=count,
                      sha256=hashlib.sha256(b).hexdigest(),records=[])
        for index in range(count):
            o=512+384*index;rawpath=text(b,o+0x10C,116);levpath=extension('level/'+rawpath,'.lev');path=extracted/levpath
            record=dict(id=i32(b,o+256),level_path=levpath,raw_level_path=rawpath,level_exists=levpath in names,
                        successors=list(struct.unpack_from('<8i',b,o)),
                        movies_variant=list(struct.unpack_from('<8i',b,o+32)),movies_zero=list(struct.unpack_from('<8i',b,o+64)),
                        carry_over_mask=u32(b,o+260),retain_previous_carry_over_mask=u32(b,o+264),conditions=[],triggers=[])
            record['exit_zones']=[dict(center_x_q12=i32(b,o+96+f*4),center_y_q12=i32(b,o+128+f*4),
                radius_q12=i32(b,o+160+f*4),destination_x_q12=i32(b,o+192+f*4),destination_y_q12=i32(b,o+224+f*4))for f in range(8)]
            if path.exists():
                lev=path.read_bytes();base=text(lev,u32(lev,0xB0),len(lev)-u32(lev,0xB0));record['embedded_level_path']=normal(base)
                record['field_path']=extension(base,'.fld');record['field_exists']=record['field_path']in names
                for slot in range(64):
                    at=0x380+slot*16;kind=lev[at]&254
                    if kind:record['conditions'].append(dict(slot=slot,kind=kind,operands=list(struct.unpack_from('<3I',lev,at+4)),payload=lev[at+1:at+16].hex()))
                for slot in range(16):
                    at=0x780+slot*8
                    if lev[at]:record['triggers'].append(dict(slot=slot,state=lev[at],variant=lev[at+1],direct_end=lev[at+2],faction=lev[at+4],outcome=lev[at+5],condition=lev[at+6]))
            campaign['records'].append(record)
        for trigger_only,key in [(False,'table_unreachable_ids'),(True,'active_trigger_unreachable_ids')]:
            reached=reachable(campaign['first_level_id'],campaign['records'],trigger_only)
            campaign[key]=[r['id']for r in campaign['records']if r['id']not in reached]
        campaign['missing_maps']=[r['level_path']for r in campaign['records']if not r['level_exists']]
        campaign['missing_terrain']=[r['field_path']for r in campaign['records']if r['level_exists']and not r.get('field_exists')]
        nums={m for r in campaign['records']for m in r['movies_variant']+r['movies_zero']}
        campaign['missing_movie_numbers']=[m for m in sorted(nums)if f'flm/ende{m:04d}.flm'not in names]
        campaigns.append(campaign)
    # All existing map assets are either standalone entries or on a main campaign's structural graph.
    covered=set()
    for p in [extracted/'level/level.dat',*sorted((extracted/'level').glob('level[0-9][0-9].dat'))]:
        if p.exists():covered.update(extension('level/'+r['identifier'],'.lev')for r in catalog(p))
    for c in campaigns:
        if c['listed']:covered.update(r['level_path']for r in c['records']if r['id']not in c['active_trigger_unreachable_ids'])
    result=dict(scope='Supplied archive inventory; structural active-trigger graph, not condition satisfiability or playthrough proof.',
                catalog=listed,campaigns=campaigns,inventory=entries,
                unaccounted_existing_levels=sorted(n for n in names if n.endswith('.lev')and n not in covered))
    (out/'campaign_census.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
    with (out/'campaign_edges.csv').open('w',newline='')as f:
        writer=csv.writer(f);writer.writerow(['campaign','listed','map_id','map','map_exists','field','field_exists','structurally_reachable','outcome','successor','transition','active_trigger_slots','condition_slots','carry_over','retain_previous_carry_over','movie_variant_zero','movie_variant_nonzero'])
        for c in campaigns:
            ids={r['id']for r in c['records']}
            for r in c['records']:
                for o,target in enumerate(r['successors']):
                    triggers=[t for t in r['triggers']if t['state']==1 and t['outcome']==o]
                    writer.writerow([c['path'],c['listed'],r['id'],r['level_path'],r['level_exists'],r.get('field_path',''),r.get('field_exists',''),r['id']not in c['active_trigger_unreachable_ids'],o,target,'no transition'if target<0 else'replay'if target==r['id']else'next map'if target in ids else'finish campaign',','.join(str(t['slot'])for t in triggers),','.join(str(t['condition'])for t in triggers),bool(r['carry_over_mask']&(1<<o)),bool(r['retain_previous_carry_over_mask']&(1<<o)),r['movies_zero'][o],r['movies_variant'][o]])
    print(f"Campaign census PASS: {len(campaigns)} CGNs, {len(listed)} listed campaigns, {sum(e['path'].endswith('.lev')for e in entries)} LEVs; unaccounted maps={result['unaccounted_existing_levels']}")
    for c in campaigns:
        if c['listed']:print(c['title'],': start',c['first_level_id'],'records',c['record_count'],'missing',c['missing_maps'],'unreachable',c['active_trigger_unreachable_ids'],'missing terrain',c['missing_terrain'])
if __name__=='__main__':main()
