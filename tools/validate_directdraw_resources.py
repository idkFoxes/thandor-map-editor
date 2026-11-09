#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, re, struct, subprocess
from pathlib import Path

IMAGE_BASE = 0x400000
PRESET_VA = 0x577E50
DISPATCH_PREFIX_VA = 0x577EA0
PRIMITIVE_TABLE_VA = 0x577EB8
RESOURCE_HANDLER_VA = 0x57E3B0
EXPECTED_PRESETS = [
    (2,1,0,1), (5,6,1,1), (5,6,1,0), (5,6,1,1), (2,2,1,0)
]
EXPECTED_HANDLERS = {
    0:0x57CCB0, 1:0x57CF20, 2:0x57D480, 4:0x57CCB0, 6:0x57D1D0,
    8:0x57CCB0, 9:0x57CF20, 10:0x57D480, 12:0x57CCB0, 14:0x57D1D0,
    16:0x57D730, 17:0x57DD70, 18:0x57E3B0, 20:0x57DA50, 22:0x57E090,
    24:0x57D730, 25:0x57DD70, 26:0x57E3B0, 28:0x57DA50, 30:0x57E090,
    32:0x57CF20, 33:0x57CF20, 34:0x57D480, 36:0x57CF20, 38:0x57CF20,
    40:0x57CF20, 41:0x57CF20, 42:0x57D480, 44:0x57CF20, 46:0x57CF20,
    48:0x57DD70, 49:0x57DD70, 50:0x57E3B0, 52:0x57DD70, 54:0x57DD70,
    56:0x57DD70, 57:0x57DD70, 58:0x57E3B0, 60:0x57DD70, 62:0x57DD70,
}
NULL_SELECTORS = {3,5,7,11,13,15,19,21,23,27,29,31,35,37,39,43,45,47,51,53,55,59,61,63}

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def va_slice(data: bytes, va: int, size: int) -> bytes:
    off = va - IMAGE_BASE
    if off < 0 or off + size > len(data):
        raise RuntimeError(f"VA 0x{va:08X} outside image")
    return data[off:off+size]

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--source', type=Path, default=Path('.'))
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--current-c', type=Path)
    ap.add_argument('--untouched-c', type=Path)
    ap.add_argument('--gzf', type=Path)
    ap.add_argument('--game-root', type=Path, required=True)
    ap.add_argument('--build-dir', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args=ap.parse_args()
    src=args.source.resolve(); binary=args.binary.read_bytes()
    checks={}
    presets=[]
    raw=va_slice(binary, PRESET_VA, 5*16)
    for i in range(5): presets.append(struct.unpack_from('<4I', raw, i*16))
    checks['directdraw_presets_exact']=presets==EXPECTED_PRESETS
    prefix=va_slice(binary, DISPATCH_PREFIX_VA, 24)
    table=va_slice(binary, PRIMITIVE_TABLE_VA, 64*4)
    pointers=list(struct.unpack('<64I', table))
    checks['primitive_table_starts_after_six_callbacks']=PRIMITIVE_TABLE_VA-DISPATCH_PREFIX_VA==24 and len(prefix)==24
    checks['all_nonnull_handlers_exact']=all(pointers[i]==v for i,v in EXPECTED_HANDLERS.items())
    checks['all_null_selectors_exact']=all(pointers[i]==0 for i in NULL_SELECTORS)
    checks['selector18_textured_preset4']=pointers[18]==RESOURCE_HANDLER_VA and presets[4]==(2,2,1,0)
    handler=va_slice(binary, RESOURCE_HANDLER_VA, 0x2D0)
    checks['resource_handler_binary_range_nonempty']=len(handler)==0x2D0 and len(set(handler))>32

    current=args.current_c.read_text(errors='replace') if args.current_c else ''
    untouched=args.untouched_c.read_text(errors='replace') if args.untouched_c else ''
    if args.current_c:
      checks['current_c_directdraw_handler']=all(x in current for x in [
        'Direct3D_PrimitiveHandler_TexturedPreset4', 'D3DRENDERSTATE_SRCBLEND',
        'D3DRENDERSTATE_DESTBLEND', 'g_PrimitiveRenderStatePresets[4].zWriteEnable'])
    if args.untouched_c:
      checks['untouched_c_handler_owner']=all(x in untouched for x in ['Direct3D_PrimitiveHandler_TexturedPreset4', 'g_PrimitiveRenderStatePresets[4].sourceBlend', 'g_PrimitiveRenderStatePresets[4].destinationBlend'])
    if args.current_c:
      checks['current_c_resource_emitter']=all(x in current for x in [
        'ArmyRuntime_UpdateTimedShotAndEffectEmitters',
        'reservedE0_EB + 8',
        '*(int *)((int)pvVar2 + 0x178)',
        '*(int *)((int)pvVar2 + 0x17c)',
        'g_RandomGeneratorState.next'])
    if args.current_c:
      checks['current_c_model_tint_bypass']=('& 0x8e00' in current or '& 0x200' in current) and ('materialPackedColor' in current or 'packedColor' in current)

    primitive=(src/'src/editor/primitive_render.hpp').read_text()
    effect=(src/'src/core/effect.cpp').read_text()
    editor=(src/'src/editor/editor_app.cpp').read_text()
    terrain=(src/'shaders/terrain.frag').read_text()
    model=(src/'shaders/model.frag').read_text()
    checks['source_exact_directdraw_table']=all(x in primitive for x in [
        '0x00577EB8', 'case 2U: case 10U: case 18U',
        'Do not invent a depth-writing dense-body override'])
    checks['source_shared_rng_schedule']=all(x in effect+editor for x in [
        'build_resource_emitter_schedule', 'next_ticks', 'interval_random',
        'resource_animation_rng_seed_', 'spawn_tick'])
    checks['source_initial_immediate_pulses']='std::vector<uint32_t> next_ticks(emitters.size(), 0U)' in effect
    checks['source_type6_attachment']='type6_attachment_points' in editor and 'SPR lookup table' in (src/'src/core/model.cpp').read_text()
    checks['source_resource_white_tint']='?0xFFFFFFFFU' in editor
    checks['source_shader_scheduled_age']=all(x in terrain+model for x in [
        'scene.materialScale.w - record.phaseGrowStopRelease.x',
        'scene.material_scale.w - record.phaseGrowStopRelease.x'])
    checks['source_generated_shading_tint']='mix(result, clamp(record.colorPeriod.rgb, 0.0, 1.0), maskAlpha)' in terrain

    exe=args.build_dir/'rendering_resource_glow_water_tests'
    run=subprocess.run([str(exe), str(args.game_root)], capture_output=True, text=True)
    checks['stock_resource_regression_exit_zero']=run.returncode==0
    checks['stock_resource_623_directdraw_triangles']='directdraw_selector18_additive=623' in run.stdout
    checks['stock_eff_profiles']='effect114_color=0x1e001e9b' in run.stdout and 'effect116_color=0x1e9b5a00' in run.stdout

    if args.gzf:
      checks['gzf_present_nonempty']=args.gzf.stat().st_size>1_000_000
    status='PASS' if all(checks.values()) else 'FAIL'
    result={
      'schema':'thandor-vulkan-map-editor-g340-directdraw-resource-validation-v2',
      'status':status,'scope':'full-cross-source' if args.current_c and args.untouched_c and args.gzf else 'target-binary-pck-source','checks':checks,
      'hashes':{
        'game.exe':sha(args.binary),
        **({'current_c':sha(args.current_c)} if args.current_c else {}),
        **({'untouched_c':sha(args.untouched_c)} if args.untouched_c else {}),
        **({'thandor.exe.gzf':sha(args.gzf)} if args.gzf else {}),
        'preset_range':hashlib.sha256(raw).hexdigest(),
        'dispatch_prefix':hashlib.sha256(prefix).hexdigest(),
        'primitive_table':hashlib.sha256(table).hexdigest(),
        'selector18_handler':hashlib.sha256(handler).hexdigest(),
      },
      'directdraw':{
        'preset_va':f'0x{PRESET_VA:08X}','primitive_table_va':f'0x{PRIMITIVE_TABLE_VA:08X}',
        'selector18_handler_va':f'0x{RESOURCE_HANDLER_VA:08X}',
        'preset4':list(presets[4]),'resource_triangle_count':623,
      },
      'stock_test_stdout':run.stdout,
      'stock_test_stderr':run.stderr,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    print(json.dumps(result,indent=2,sort_keys=True))
    return 0 if status=='PASS' else 1
if __name__=='__main__': raise SystemExit(main())
