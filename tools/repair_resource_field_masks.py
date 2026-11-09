#!/usr/bin/env python3
"""Repair visible resource deposits whose FLD build-support masks are missing.

The immutable game treats 0x0800/0x1000 as positive placement support for the two
extractor classes. Visible ARM 814/815 deposits need 0x0800; ARM 810/811 deposits
need 0x1000. This tool also carries forward the G335 dry-water, starting-resource,
and PATCH shadow repairs so an old submitted package can be repaired in one pass.
"""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path
from repair_exported_map_startup_assets import parse_stored_pck, build_stored_pck, u32, put_u32, i32
from repair_zero_water_and_starting_resources import repair_field, repair_level, root_map_chain

FIELD_HEADER=0x200
FIELD_CELL=0x80
RESOURCE_CLASS={810:1,811:1,814:0,815:0}
STAMP_RADIUS=2

def sha256(path: Path)->str:
    h=hashlib.sha256(); h.update(path.read_bytes()); return h.hexdigest()

def placements(payload: bytes):
    count=u32(payload,0xD8); start=u32(payload,0xDC)
    if start+count*0x20!=len(payload): raise ValueError('LEV placement count/offset mismatch')
    for index in range(count):
        off=start+index*0x20
        yield index,u32(payload,off),i32(payload,off+0x08),i32(payload,off+0x0C)

def nearest_cell(field: bytes, render_x: int, render_y: int)->tuple[int,int]:
    w,h=u32(field,0xB8),u32(field,0xBC)
    best=None
    for y in range(h):
        for x in range(w):
            off=FIELD_HEADER+(y*w+x)*FIELD_CELL
            dx=i32(field,off+0x40)-render_x
            dy=i32(field,off+0x44)-render_y
            distance=dx*dx+dy*dy
            if best is None or distance<best[0]: best=(distance,x,y)
    assert best is not None
    return best[1],best[2]

def stamp_resource_fields(field_payload: bytes, level_payload: bytes):
    out=bytearray(field_payload); w,h=u32(out,0xB8),u32(out,0xBC)
    changed_by_class={0:0,1:0}; placements_by_class={0:0,1:0}; records=[]
    for index,asset_id,render_x,render_y in placements(level_payload):
        occupancy_class=RESOURCE_CLASS.get(asset_id)
        if occupancy_class is None: continue
        cx,cy=nearest_cell(out,render_x,render_y)
        mask=0x800<<occupancy_class
        changed=0
        for y in range(max(0,cy-STAMP_RADIUS),min(h,cy+STAMP_RADIUS+1)):
            for x in range(max(0,cx-STAMP_RADIUS),min(w,cx+STAMP_RADIUS+1)):
                off=FIELD_HEADER+(y*w+x)*FIELD_CELL+0x50
                before=u32(out,off); after=before|mask
                if after!=before: put_u32(out,off,after); changed+=1
        placements_by_class[occupancy_class]+=1
        changed_by_class[occupancy_class]+=changed
        records.append({'placement_index':index,'asset_id':asset_id,'occupancy_class':occupancy_class,
                        'required_mask':f'0x{mask:08X}','center_cell':[cx,cy],
                        'newly_set_cells':changed})
    return bytes(out),{
        'stamp_shape':'square','stamp_radius_cells':STAMP_RADIUS,'stamp_size':'5x5',
        'energy':{'occupancy_class':1,'mask':'0x00001000','placements':placements_by_class[1],
                  'newly_set_cells':changed_by_class[1]},
        'building_material':{'occupancy_class':0,'mask':'0x00000800','placements':placements_by_class[0],
                  'newly_set_cells':changed_by_class[0]},
        'records':records,
    }

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
    entries[fi[0]].data,water_report=repair_field(entries[fi[0]].data)
    entries[li[0]].data,level_report=repair_level(entries[li[0]].data)
    entries[fi[0]].data,mask_report=stamp_resource_fields(entries[fi[0]].data,entries[li[0]].data)
    a.output_level.write_bytes(build_stored_pck(header,entries))

    ph,pe=parse_stored_pck(a.patch00)
    removed=[e.name for e in pe if root_map_chain(e.name)]
    pe=[e for e in pe if not root_map_chain(e.name)]
    a.output_patch.write_bytes(build_stored_pck(ph,pe))

    # Independent reparse and per-resource center/corner proof.
    _,ve=parse_stored_pck(a.output_level)
    fld=next(e.data for e in ve if e.name.lower().endswith('.fld'))
    lev=next(e.data for e in ve if e.name.lower().endswith('.lev'))
    w,h=u32(fld,0xB8),u32(fld,0xBC)
    validated=0
    for _,asset_id,rx,ry in placements(lev):
        cls=RESOURCE_CLASS.get(asset_id)
        if cls is None: continue
        cx,cy=nearest_cell(fld,rx,ry); mask=0x800<<cls
        for y in range(max(0,cy-1),min(h,cy+2)):
            for x in range(max(0,cx-1),min(w,cx+2)):
                assert u32(fld,FIELD_HEADER+(y*w+x)*FIELD_CELL+0x50)&mask
        validated+=1
    _,vpe=parse_stored_pck(a.output_patch)
    assert not any(root_map_chain(e.name) for e in vpe)
    report={
      'schema':'thandor-g336-resource-field-support-mask-repair-v1','status':'PASS',
      'input':{'level':str(a.level_package),'level_sha256':sha256(a.level_package),
               'patch00':str(a.patch00),'patch00_sha256':sha256(a.patch00)},
      'output':{'level':str(a.output_level),'level_sha256':sha256(a.output_level),
                'patch00':str(a.output_patch),'patch00_sha256':sha256(a.output_patch),
                'patch_map_chain_removed':removed},
      'water':water_report,'level':level_report,'resource_fields':mask_report,
      'validated_resource_placements':validated,
    }
    a.report.write_text(json.dumps(report,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2,sort_keys=True))
    return 0
if __name__=='__main__': raise SystemExit(main())
