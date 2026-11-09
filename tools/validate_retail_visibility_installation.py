#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, struct
from pathlib import Path
GAME_SHA256='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def sections(d):
 p=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,p+6)[0]; o=struct.unpack_from('<H',d,p+20)[0]; t=p+24+o
 for i in range(n):
  q=t+i*40; vs,va,rs,raw=struct.unpack_from('<IIII',d,q+8); yield va,max(vs,rs),raw
def vb(d,va,n):
 r=va-0x400000
 for b,s,raw in sections(d):
  if b<=r<b+s: return d[raw+r-b:raw+r-b+n]
 raise ValueError(hex(va))
def main():
 ap=argparse.ArgumentParser(); ap.add_argument('--source',type=Path,required=True); ap.add_argument('--game-exe',type=Path,required=True); ap.add_argument('--output',type=Path,required=True); a=ap.parse_args()
 d=a.game_exe.read_bytes(); s=a.source
 field=(s/'src/core/field.cpp').read_text(); h=(s/'include/thandor/field.hpp').read_text(); e=(s/'src/editor/editor_app.cpp').read_text(); sh=(s/'shaders/terrain.vert').read_text()
 checks={
  'immutable_game':sha(a.game_exe)==GAME_SHA256,
  'signed_anchor':vb(d,0x5056d0,16)==bytes.fromhex('C1 FB 10 8B 15 94 19 50 00 8B 04 9D 90 15 50 00'),
  'full_visibility_init':vb(d,0x503d26,10)==bytes.fromhex('C7 86 04 00 00 00 FF FF FF FF'),
  'signed_source':all(x in field for x in ['signed_dot * 256.0F','256 + signed_index','if (signed_index >= 0) return ramp_124']),
  'old_mapping_absent':'(1.0F - intensity) * 127.5F' not in field,
  'fully_visible_source':'kFullyVisibleOverlayArgb = 0xFFFFFFFFU' in h and 'fullVisibilityOverlayBytes = vec3(255.0)' in sh,
  'terrain_and_models':all(x in field+e for x in ['directional_color_for_upward_normal(normals[index], ramp)','level_lighting.directional_color_for_upward_normal(normal,ramp)']),
  'shadow_ray_retained':'shadow_projection_direction=document_->level().lighting().shadow_ray_direction()' in e,
 }
 bad=[k for k,v in checks.items() if not v]
 if bad: raise SystemExit('G358 install contract failed: '+', '.join(bad))
 out={'schema':'thandor-map-editor-g358-install-contract-v1','status':'PASS','checks':checks,'game_exe_sha256':sha(a.game_exe)}
 a.output.parent.mkdir(parents=True,exist_ok=True); a.output.write_text(json.dumps(out,indent=2,sort_keys=True)+'\n'); print(json.dumps(out,indent=2,sort_keys=True))
if __name__=='__main__': main()
