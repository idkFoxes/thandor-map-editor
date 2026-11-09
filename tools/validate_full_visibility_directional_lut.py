#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, struct
from pathlib import Path

GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"

def sha256(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1<<20),b''): h.update(chunk)
    return h.hexdigest()

def pe_sections(data: bytes):
    pe=struct.unpack_from('<I',data,0x3c)[0]
    count=struct.unpack_from('<H',data,pe+6)[0]
    opt=struct.unpack_from('<H',data,pe+20)[0]
    table=pe+24+opt
    out=[]
    for i in range(count):
        off=table+i*40
        name=data[off:off+8].split(b'\0')[0].decode('ascii','replace')
        vsize,va,rsize,raw=struct.unpack_from('<IIII',data,off+8)
        out.append((name,va,max(vsize,rsize),raw))
    return out

def va_bytes(data: bytes, va: int, size: int) -> bytes:
    rva=va-0x400000
    for _,base,span,raw in pe_sections(data):
        if base<=rva<base+span:
            off=raw+(rva-base)
            return data[off:off+size]
    raise ValueError(hex(va))

def exact(data:bytes,va:int,hexbytes:str)->bool:
    expected=bytes.fromhex(hexbytes)
    return va_bytes(data,va,len(expected))==expected

def require(ok:bool,msg:str):
    if not ok: raise SystemExit(msg)

def main()->int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--source',type=Path,required=True)
    ap.add_argument('--game-exe',type=Path,required=True)
    ap.add_argument('--current-c',type=Path,required=True)
    ap.add_argument('--untouched-c',type=Path,required=True)
    ap.add_argument('--gzf',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args(); src=a.source.resolve(); game=a.game_exe.read_bytes()
    field_hpp=(src/'include/thandor/field.hpp').read_text()
    field_cpp=(src/'src/core/field.cpp').read_text()
    editor=(src/'src/editor/editor_app.cpp').read_text()
    shader=(src/'shaders/terrain.vert').read_text()
    tests=(src/'tests/visibility_full_visibility_directional_lut_tests.cpp').read_text()
    cmake=(src/'CMakeLists.txt').read_text()
    gate=(src/'docs/G358_FULL_VISIBILITY_AND_SIGNED_DIRECTIONAL_LIGHT_LUT_GATE.md').read_text()
    current=a.current_c.read_text(errors='replace'); untouched=a.untouched_c.read_text(errors='replace')
    checks={
      'immutable_game_sha256':sha256(a.game_exe)==GAME_SHA256,
      'signed_lut_anchor_load':exact(game,0x005056D0,'C1 FB 10 8B 15 94 19 50 00 8B 04 9D 90 15 50 00'),
      'ramp_storage_starts_501190':exact(game,0x00505789,'BF 90 11 50 00'),
      'fixed_base_table_starts_501590':exact(game,0x0050587E,'BF 90 15 50 00 B9 01 01 00 00'),
      'field_initialization_sets_full_visibility':exact(game,0x00503D26,'C7 86 04 00 00 00 FF FF FF FF'),
      'directdraw_multiplies_directional_light_by_cell_overlay':exact(game,0x0050054C,'0F 6E 86 58 00 00 00 0F 6E 8E 04 00 00 00'),
      'c_names_directional_lut_and_overlay':all(t in current and t in untouched for t in [
          'FieldGridCell_ComputeDirectionalLightColor','g_TerrainDirectionalLightColorLut',
          'FieldGrid_SetAllCellOverlayColors']),
      'source_signed_lut_mapping':all(t in field_cpp for t in [
          'signed_dot * 256.0F','256 + signed_index','if (signed_index >= 0) return ramp_124',
          '0x00501590','0x00501190']),
      'old_symmetric_mapping_absent':'(1.0F - intensity) * 127.5F' not in field_cpp and 'light_index' not in editor,
      'terrain_and_models_share_mapping':all(t in field_cpp+editor for t in [
          'directional_color_for_upward_normal(normals[index], ramp)',
          'level_lighting.directional_color_for_upward_normal(normal,ramp)']),
      'shadow_uses_ray_direction': 'shadow_projection_direction=document_->level().lighting().shadow_ray_direction()' in editor,
      'full_visibility_constant': 'kFullyVisibleOverlayArgb = 0xFFFFFFFFU' in field_hpp,
      'shader_full_visibility':all(t in shader for t in [
          'fullVisibilityOverlayBytes = vec3(255.0)','0x00503D26','fully visible/no-fog state']),
      'editor_declares_fow_disabled':'fully visible (fog of war disabled; cell overlay 0xFFFFFFFF)' in editor,
      'regression_registered':all(t in cmake+tests for t in [
          'visibility_full_visibility_directional_lut_tests',
          'G358 full-visibility and signed directional-LUT tests passed']),
      'task_gate_present':all(t in gate for t in ['0x00501590','0x00501190','0x00503D26','0xFFFFFFFF','Research-only output fails']),
      'gzf_present':a.gzf.is_file() and a.gzf.stat().st_size>0,
    }
    for k,v in checks.items(): require(v,f'G358 validation failed: {k}')
    result={
      'schema':'thandor-map-editor-g358-full-visibility-signed-directional-lut-v1',
      'status':'PASS','checks':checks,
      'hashes':{'game.exe':sha256(a.game_exe),'current_c':sha256(a.current_c),
                'untouched_c':sha256(a.untouched_c),'supplied_gzf':sha256(a.gzf)},
      'recovered_contract':{
        'directional_lut_anchor':'0x00501590','generated_ramp':'0x00501190..0x0050158F',
        'signed_index_range':'-256..+256','negative_index_mapping':'ramp[256 + signedIndex]',
        'zero_or_positive_mapping':'fixed rampColor124 base table',
        'default_cell_overlay':'0xFFFFFFFF at FieldGridCell +0x04',
        'shadow_direction':'LEV light-ray direction unchanged',
        'preview_visibility':'fully visible; no fog-of-war attenuation'},
      'limitations':{
        'c_inputs_are_duplicate':sha256(a.current_c)==sha256(a.untouched_c),
        'gzf_note':'The supplied GZF is comparison evidence; immutable game.exe bytes are the authority for the signed table anchor and full-visibility initialization.',
        'visual_note':'A live target Vulkan screenshot remains required for final pixel-level comparison.'}
    }
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    print(json.dumps(result,indent=2,sort_keys=True)); return 0
if __name__=='__main__': raise SystemExit(main())
