#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, subprocess
from pathlib import Path
from repair_exported_map_startup_assets import parse_stored_pck, u32, i32
from repair_resource_field_masks import FIELD_HEADER, FIELD_CELL, RESOURCE_CLASS, nearest_cell, placements, root_map_chain

def sha(path: Path)->str:
    h=hashlib.sha256(); h.update(path.read_bytes()); return h.hexdigest()

def require(v: bool, m: str):
    if not v: raise SystemExit(m)

def main()->int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--source-root',type=Path,required=True)
    ap.add_argument('--game-exe',type=Path,required=True)
    ap.add_argument('--level-package',type=Path,required=True)
    ap.add_argument('--patch00',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args(); s=a.source_root.resolve()
    field=(s/'src/core/field.cpp').read_text(); catalog=(s/'src/core/catalog.cpp').read_text()
    editor=(s/'src/editor/editor_app.cpp').read_text(); doc=(s/'src/core/map_document.cpp').read_text()
    build_linux=s/'build_linux.sh'
    checks={
      'positive_support_polarity': all(t in field for t in ['placement_occupancy_class_present','!placement_occupancy_class_present','0x00000800U << occupancy_class']),
      'exact_resource_id_mapping': all(t in catalog for t in ['case 810U','case 811U','case 814U','case 815U','return 1U','return 0U']),
      'new_placement_auto_stamp': all(t in editor for t in ['resource_field_occupancy_class(placed_asset_id)','radius_cells=2.0F','BrushShape::Square']),
      'export_legacy_sync': 'stamp_resource_field_masks(emitted_field, emitted_level.placements())' in doc,
      'manual_paint_erase': all(t in editor for t in ['Energy field (blue)','Building field (gold)','Paint buildable field','Erase buildable field','Tool::ResourceField']),
      'overlay_polarity_text': "missing the selected structure's required" in editor,
      'complete_source_build_entrypoint': build_linux.is_file() and build_linux.stat().st_mode & 0o111 != 0 and 'THANDOR_EDITOR_BUILD_APP=ON' in build_linux.read_text(),
    }
    for n,v in checks.items(): require(v,f'G336 source check failed: {n}')
    dis=subprocess.check_output(['objdump','-d','-Mintel','--start-address=0x524fe9','--stop-address=0x525015',str(a.game_exe)],text=True)
    for token in ['mov    eax,0x800','test   DWORD PTR [edi+0x50],eax','je     0x52500d','clc','stc']:
        require(token in dis,f'binary placement polarity token missing: {token}')
    _,entries=parse_stored_pck(a.level_package)
    fld=next(e.data for e in entries if e.name.lower().endswith('.fld'))
    lev=next(e.data for e in entries if e.name.lower().endswith('.lev'))
    w,h=u32(fld,0xB8),u32(fld,0xBC); counts={0:0,1:0}; validated=0
    for _,asset,rx,ry in placements(lev):
        cls=RESOURCE_CLASS.get(asset)
        if cls is None: continue
        cx,cy=nearest_cell(fld,rx,ry); mask=0x800<<cls
        for y in range(max(0,cy-1),min(h,cy+2)):
            for x in range(max(0,cx-1),min(w,cx+2)):
                require(u32(fld,FIELD_HEADER+(y*w+x)*FIELD_CELL+0x50)&mask,
                        f'ARM {asset} missing support at {x},{y}')
        counts[cls]+=1; validated+=1
    _,patch_entries=parse_stored_pck(a.patch00)
    require(not any(root_map_chain(e.name) for e in patch_entries),'PATCH root map shadow remains')
    result={'schema':'thandor-g336-resource-field-validation-v1','status':'PASS',
      'checks':checks,'binary':{'game_sha256':sha(a.game_exe),'placement_disassembly_verified':True},
      'repaired_map':{'level_sha256':sha(a.level_package),'patch00_sha256':sha(a.patch00),
        'validated_resource_placements':validated,'energy_placements':counts[1],
        'building_material_placements':counts[0]}}
    a.output.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    print(json.dumps(result,indent=2,sort_keys=True)); return 0
if __name__=='__main__': raise SystemExit(main())
