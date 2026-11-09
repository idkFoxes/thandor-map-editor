#!/usr/bin/env python3
"""Repair G334 exports whose zero FLD water delta is wet to game.exe and whose LEV shell has zero resources."""
from __future__ import annotations
import argparse, hashlib, json, struct
from pathlib import Path
from repair_exported_map_startup_assets import (Entry, parse_stored_pck, build_stored_pck, u32, put_u32, i32, put_i32)

FIELD_HEADER=0x200
FIELD_CELL=0x80
FIELD_MAGIC=0x00646C66
LEVEL_MAGIC=0x0076656C
DRY_DELTA_Q12=-0x1000
DEFAULT_BUILDING_Q4=6500*16
DEFAULT_ENERGY_Q4=3000*16

def sha256(path: Path)->str:
    h=hashlib.sha256(); h.update(path.read_bytes()); return h.hexdigest()

def root_map_chain(name: str)->bool:
    n=name.replace('/','\\').lower()
    if not n.startswith('level\\'): return False
    tail=n[6:]
    return '\\' not in tail and tail.endswith(('.lev','.fld','.str','.dat'))

def repair_field(payload: bytes):
    if len(payload)<FIELD_HEADER or u32(payload,0)!=FIELD_MAGIC: raise ValueError('invalid FLD')
    w,h=u32(payload,0xB8),u32(payload,0xBC)
    if len(payload)!=FIELD_HEADER+w*h*FIELD_CELL: raise ValueError('FLD size mismatch')
    out=bytearray(payload); repaired=0; positive=0; negative=0
    for idx in range(w*h):
        off=FIELD_HEADER+idx*FIELD_CELL+0x4C
        value=i32(out,off)
        if value==0: put_i32(out,off,DRY_DELTA_Q12); repaired+=1
        elif value>0: positive+=1
        else: negative+=1
    return bytes(out), {'width':w,'height':h,'zero_cells_repaired':repaired,'positive_water_cells_preserved':positive,'negative_dry_cells_preserved':negative,'dry_delta_q12':DRY_DELTA_Q12}

def repair_level(payload: bytes):
    if len(payload)<0x320 or u32(payload,0)!=LEVEL_MAGIC: raise ValueError('invalid LEV')
    out=bytearray(payload)
    players=min(u32(out,0x314),7)
    changed=[]
    for faction in range(1,players+1):
        base=0x200+(faction-1)*0x20
        building=u32(out,base+0x14); energy=u32(out,base+0x18)
        if building==0 and energy==0:
            put_u32(out,base+0x14,DEFAULT_BUILDING_Q4)
            put_u32(out,base+0x18,DEFAULT_ENERGY_Q4)
            changed.append(faction)
    return bytes(out), {'active_factions':players,'factions_defaulted':changed,'building_resource_q4':DEFAULT_BUILDING_Q4,'energy_resource_q4':DEFAULT_ENERGY_Q4}

def main()->int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--level-package',type=Path,required=True)
    ap.add_argument('--patch00',type=Path,required=True)
    ap.add_argument('--output-level',type=Path,required=True)
    ap.add_argument('--output-patch',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    a=ap.parse_args()
    header,entries=parse_stored_pck(a.level_package)
    fi=[i for i,e in enumerate(entries) if e.name.lower().endswith('.fld')]
    li=[i for i,e in enumerate(entries) if e.name.lower().endswith('.lev')]
    if len(fi)!=1 or len(li)!=1: raise ValueError('expected exactly one FLD and LEV')
    entries[fi[0]].data, field_report=repair_field(entries[fi[0]].data)
    entries[li[0]].data, level_report=repair_level(entries[li[0]].data)
    a.output_level.write_bytes(build_stored_pck(header,entries))
    ph,pe=parse_stored_pck(a.patch00)
    removed=[e.name for e in pe if root_map_chain(e.name)]
    pe=[e for e in pe if not root_map_chain(e.name)]
    a.output_patch.write_bytes(build_stored_pck(ph,pe))
    # independent reparse assertions
    _,ve=parse_stored_pck(a.output_level)
    fld=next(e.data for e in ve if e.name.lower().endswith('.fld'))
    lev=next(e.data for e in ve if e.name.lower().endswith('.lev'))
    w,h=u32(fld,0xB8),u32(fld,0xBC)
    assert all(i32(fld,FIELD_HEADER+i*FIELD_CELL+0x4C)!=0 for i in range(w*h))
    for faction in level_report['factions_defaulted']:
        base=0x200+(faction-1)*0x20
        assert u32(lev,base+0x14)==DEFAULT_BUILDING_Q4 and u32(lev,base+0x18)==DEFAULT_ENERGY_Q4
    _,vpe=parse_stored_pck(a.output_patch)
    assert not any(root_map_chain(e.name) for e in vpe)
    report={'schema':'thandor-g335-zero-water-starting-resource-patch-shadow-repair-v1','status':'PASS',
      'input':{'level':str(a.level_package),'level_sha256':sha256(a.level_package),'patch00':str(a.patch00),'patch00_sha256':sha256(a.patch00)},
      'output':{'level':str(a.output_level),'level_sha256':sha256(a.output_level),'patch00':str(a.output_patch),'patch00_sha256':sha256(a.output_patch),'patch_map_chain_removed':removed},
      'field':field_report,'level':level_report}
    a.report.write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print(json.dumps(report,indent=2,sort_keys=True))
    return 0
if __name__=='__main__': raise SystemExit(main())
