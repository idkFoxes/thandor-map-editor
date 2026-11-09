#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

EXPECTED = {
    "game_exe": "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969",
    "gzf": "feb31b7fe5fc32997898955b7dfd880a8fc24dc2a66aa25017e5147d126f9dc7",
    "current_c": "bb03d532fb1b89b22cdca18ef091ca3979fd5102387da765bc9ea8c882dbdf9b",
    "untouched_c": "eeea2dc72d85060054937540719d71b2c5ecc150c7af70f1358ed754f173b2d5",
}
RANGES = {
    "ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments": (0x00523980, 0x00523CA0, "2507690f719488d6b7fb1c145367e0bd977ea8e623d3f345038ec465dea39c4c"),
    "ArmyRuntime_ResolveShotLaunchFromModelAttachmentCf": (0x00529720, 0x005297D0, "d2717e7541544af9cff9f5b70eb1ded0bde8b039c4688cf32cd5ce711973624b"),
    "ShotRuntimePool_CreateProjectileFromDefinitionCf": (0x0052BDB0, 0x0052C1A0, "b3bfb90b496811c44b51a532cac4be6faff9fdf5fcc15164a2110dd807dbd56b"),
    "ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects": (0x0052C230, 0x0052CEE0, "c12cf635c509003f12f00b6b46cbbdeb58e17dcdfd2acec58894166a4fc36d8d"),
}

def sha256(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1<<20),b''): h.update(chunk)
    return h.hexdigest()

def require(condition: bool, message: str) -> None:
    if not condition: raise SystemExit(message)

def pe_slice(data: bytes, start_va: int, end_va: int) -> bytes:
    pe=struct.unpack_from('<I',data,0x3c)[0]
    require(data[pe:pe+4]==b'PE\0\0','game.exe is not a PE image')
    count=struct.unpack_from('<H',data,pe+6)[0]
    optional_size=struct.unpack_from('<H',data,pe+20)[0]
    optional=pe+24
    image_base=struct.unpack_from('<I',data,optional+28)[0]
    rva=start_va-image_base; size=end_va-start_va; table=optional+optional_size
    for index in range(count):
        off=table+index*40
        virtual_size,virtual_address,raw_size,raw_offset=struct.unpack_from('<IIII',data,off+8)
        if virtual_address<=rva and rva+size<=virtual_address+max(virtual_size,raw_size):
            start=raw_offset+rva-virtual_address
            return data[start:start+size]
    raise SystemExit(f'unmapped PE range {start_va:#x}-{end_va:#x}')

def body(text: str, name: str) -> str:
    for match in re.finditer(re.escape(name),text):
        line=text.rfind('\n',0,match.start())+1
        if line<len(text) and (text[line].isspace() or text.startswith('//',line)): continue
        cursor=match.end()
        while cursor<len(text) and text[cursor].isspace(): cursor+=1
        if cursor>=len(text) or text[cursor]!='(': continue
        brace=text.find('{',cursor)
        if brace<0: continue
        depth=0
        for end in range(brace,len(text)):
            if text[end]=='{': depth+=1
            elif text[end]=='}':
                depth-=1
                if depth==0: return text[line:end+1]
    raise SystemExit(f'cannot extract C body {name}')

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--source',type=Path,required=True)
    ap.add_argument('--previous-source',type=Path,required=True)
    ap.add_argument('--game-exe',type=Path,required=True)
    ap.add_argument('--gzf',type=Path,required=True)
    ap.add_argument('--current-c',type=Path,required=True)
    ap.add_argument('--untouched-c',type=Path,required=True)
    ap.add_argument('--game-root',type=Path)
    ap.add_argument('--stock-test',type=Path)
    ap.add_argument('--screenshot',type=Path)
    ap.add_argument('--output',type=Path)
    args=ap.parse_args()
    source=args.source.resolve(); previous=args.previous_source.resolve()
    authorities={
        'game_exe':sha256(args.game_exe),'gzf':sha256(args.gzf),
        'current_c':sha256(args.current_c),'untouched_c':sha256(args.untouched_c),
    }
    require(authorities==EXPECTED,f'authority hash mismatch: {authorities}')
    exe=args.game_exe.read_bytes(); ranges={}
    for name,(start,end,expected) in RANGES.items():
        payload=pe_slice(exe,start,end); actual=hashlib.sha256(payload).hexdigest()
        require(actual==expected,f'binary range mismatch for {name}: {actual}')
        ranges[name]={'start_va':f'0x{start:08X}','end_va_exclusive':f'0x{end:08X}',
                      'bytes':len(payload),'sha256':actual}

    current=args.current_c.read_text(encoding='utf-8',errors='replace')
    untouched=args.untouched_c.read_text(encoding='utf-8',errors='replace')
    names=list(RANGES)
    current_bodies={name:body(current,name) for name in names}
    untouched_bodies={name:body(untouched,name) for name in names}
    creation=current_bodies['ShotRuntimePool_CreateProjectileFromDefinitionCf']
    maintenance=current_bodies['ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects']
    weapon=current_bodies['ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments']
    resolver=current_bodies['ArmyRuntime_ResolveShotLaunchFromModelAttachmentCf']
    contracts={
        'creation_stores_launch_angles_and_zero_initial_mode3_speed': all(t in creation for t in [
            'worldRotationAngle0 = (int)uVar26','worldRotationAngle1 = (int)((ulonglong)uVar26 >> 0x20)',
            'worldRotationAngle2 = (dword)uVar26','if (shotDefinition->rangeScale270Q12 != 0)',
            'dVar3 = shotDefinition->elevationOffsetAngle16']),
        'maintenance_subtracts_visual_offset_from_movement_pitch':
            '(modelNode->modelPayload).worldRotationAngle1 -\n                            shotRuntime->movementStateFlags' in maintenance,
        'maintenance_quadratic_speed_ramp_and_visual_decay': all(t in maintenance for t in [
            'pSVar17->rangeScale270Q12','pSVar17->launchSpeedQ12',
            'shotRuntime->movementStateFlags = shotRuntime->movementStateFlags + iVar12',
            '*pAVar5 = *pAVar5 + iVar12']),
        'resolver_rebuilds_live_pose_and_scans_exact_kind2_selector': all(t in resolver for t in [
            'ModelNodeRuntime_RebuildTransformsFromRoot','(launchMode << 4 | 2)',
            'ModelNodeRuntime_TransformLocalPointRegs','ShotRuntimePool_CreateProjectileFromDefinitionCf']),
        'class9_uses_resolver_prefix_clear_and_independent_restore': all(t in weapon for t in [
            'ArmyRuntime_ResolveShotLaunchFromModelAttachmentCf','*pMVar2 = *pMVar2 & -2 <<',
            '*pMVar2 = *pMVar2 | 1','*pMVar2 = *pMVar2 | 2','*pMVar2 = *pMVar2 | 4']),
        'untouched_export_agrees_on_all_four_bodies': all(name in untouched_bodies and len(untouched_bodies[name])>100 for name in names),
    }
    for name,passed in contracts.items(): require(passed,f'C correspondence contract failed: {name}')

    files={
        'simulation':source/'src/core/simulation.cpp','simulation_hpp':source/'include/thandor/simulation.hpp',
        'editor':source/'src/editor/editor_app.cpp','test':source/'tests/visibility_mode3_direct_path_launch_reload_visibility_tests.cpp',
        'cmake':source/'CMakeLists.txt',
    }
    text={k:p.read_text(encoding='utf-8') for k,p in files.items()}
    active=text['simulation']+'\n'+text['editor']
    source_contracts={
        'mode3_velocity_uses_direct_pitch_not_visual_elevation': all(t in text['simulation'] for t in [
            'movement_pitch_turn16','static_cast<int64_t>(aim.pitch_turn16)-shot.elevation_offset_angle16',
            'const float direct_distance=distance3(launch,target_position)']),
        'renderer_uses_single_decaying_visual_offset': all(t in text['simulation']+text['editor'] for t in [
            'simulation_mode3_model_pitch_turn16','modelPitch-movementStateFlags']),
        'shot_origin_comes_from_exact_live_kind2_attachment': all(t in text['editor'] for t in [
            'transform_point(transforms[launch_node],attachment.local_translation)',
            'Rebuild after the aim/recoil update','simulation_create_volley']),
        'class9_reload_visibility_tracks_independent_timers': all(t in text['simulation']+text['editor'] for t in [
            'simulation_class9_render_mesh_mask','attachment_reload_ticks[selector]==0U',
            'visible_mesh_group_mask&=thandor::simulation_class9_render_mesh_mask']),
        'g380_test_registered': all(t in text['cmake'] for t in [
            'visibility_mode3_direct_path_launch_reload_visibility_tests',
            'tests/visibility_mode3_direct_path_launch_reload_visibility_tests.cpp']),
        'stock_asset352_exact_launch_and_timing_tested': all(t in text['test'] for t in [
            'catalog.army_preset(352U)','stock_launch_points.at(0U)','stock_launch_points.at(1U)',
            'stock_launch_points.at(2U)','inter_shot_ticks != 53U','reload_ticks != 400U',
            'G380_MODE3_DIRECT_PATH_LAUNCH_RELOAD PASS']),
    }
    for name,passed in source_contracts.items(): require(passed,f'source contract failed: {name}')
    rejected={
        'horizontal_only_mode3_travel':'const float horizontal=std::hypot(target_position.x-launch.x,target_position.y-launch.y)',
        'double_added_visual_elevation':'projectile.launch_pitch_turn16)+\n                static_cast<int32_t>(thandor::simulation_mode3_elevation_offset_turn16',
    }
    for name,token in rejected.items(): require(token not in active,f'rejected behavior returned: {name}')
    previous_text={
        'simulation':(previous/'src/core/simulation.cpp').read_text(encoding='utf-8'),
        'editor':(previous/'src/editor/editor_app.cpp').read_text(encoding='utf-8'),
    }
    predecessor_defects={
        'mode3_velocity_used_visual_launch_pitch':'const float pitch=turn16_to_radians(aim.pitch_turn16)' in previous_text['simulation'],
        'mode3_travel_used_horizontal_distance':'const float horizontal=std::hypot(target_position.x-launch.x,target_position.y-launch.y)' in previous_text['simulation'],
        'renderer_double_added_elevation':'simulation_mode3_elevation_offset_turn16' in previous_text['editor'] and
            'projectile.launch_pitch_turn16' in previous_text['editor'],
    }
    for name,present in predecessor_defects.items(): require(present,f'predecessor defect evidence missing: {name}')

    stock_output=None
    if args.stock_test:
        require(args.game_root is not None,'--stock-test requires --game-root')
        run=subprocess.run([str(args.stock_test),str(args.game_root)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        require(run.returncode==0 and 'G380_MODE3_DIRECT_PATH_LAUNCH_RELOAD PASS' in run.stdout,
                'G380 stock test failed:\n'+run.stdout)
        stock_output=run.stdout.strip()
    screenshot=None
    if args.screenshot:
        screenshot={'path':str(args.screenshot.resolve()),'sha256':sha256(args.screenshot),'diagnostic_only':True,
                    'reported_symptoms':['rocket flies on wrong path','shot starts at wrong point/pose',
                                         'reloaded rockets remain absent from tower rack']}
    result={
        'schema':'thandor-vulkan-map-editor-mode3-direct-path-exact-launch-reload-visibility-v1',
        'status':'PASS','authorities':authorities,
        'gzf_status':{'supplied_hash':authorities['gzf'],
                      'role':'serialized comparison evidence; accepted semantic authority is the newer V281 chain',
                      'promoted':False},
        'binary_ranges':ranges,'current_and_untouched_c_contracts':contracts,
        'current_c_body_sha256':{n:hashlib.sha256(b.encode()).hexdigest() for n,b in current_bodies.items()},
        'untouched_c_body_sha256':{n:hashlib.sha256(b.encode()).hexdigest() for n,b in untouched_bodies.items()},
        'source_contracts':source_contracts,'predecessor_defects':predecessor_defects,
        'stock_test':stock_output,'screenshot':screenshot,
        'asset_352_contract':{
            'runtime_class':9,'shot_definition':20,'selectors':[0,1,2],
            'selector_mask':'0x07','inter_shot_ticks':53,'reload_ticks':400,
            'movement':'direct launch-to-target line with quadratic speed ramp',
            'visual_pitch':'serialized elevation offset decays independently from movement direction',
        },
    }
    out=json.dumps(result,indent=2,sort_keys=True)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text(out,encoding='utf-8')
    else: print(out,end='')
    return 0

if __name__=='__main__': raise SystemExit(main())
