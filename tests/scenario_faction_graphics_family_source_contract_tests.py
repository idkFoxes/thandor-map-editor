#!/usr/bin/env python3
from pathlib import Path
import argparse, sys

def need(text, token, message):
    if token not in text:
        raise RuntimeError(message + ": missing " + token)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--source', required=True); args=ap.parse_args()
    root=Path(args.source)
    editor=(root/'src/editor/editor_app.cpp').read_text()
    level_h=(root/'include/thandor/level.hpp').read_text()
    level_cpp=(root/'src/core/level.cpp').read_text()
    catalog=(root/'src/core/catalog.cpp').read_text()
    need(level_h, 'return faction_index == 0U ? 0U : faction_index + raw_level_offset;',
         'retail 32-bit faction graphics selector formula drifted')
    need(level_cpp, 'resolve_faction_graphics_family(faction_index, faction_graphics_family_offset_raw(faction_index))',
         'LEV graphics selector no longer consumes raw player-slot field')
    need(editor, 'graphics_family_for_faction(placement.faction_index)',
         'placed entities no longer use LEV-resolved graphics family')
    need(editor, 'graphics_family_for_faction(entity.faction_index)',
         'runtime entities no longer use LEV-resolved graphics family')
    need(editor, 'Graphics family offset (raw LEV)',
         'frontend-authored signed graphics offset is not exposed accurately')
    if 'ai_class_or_mode=std::clamp(ai_class_or_mode,0' in editor:
        raise RuntimeError('negative retail color offsets are still destructively clamped')
    need(catalog, '{"Lime / olive",   "Thoraner",  0x00DD00U, 0x7A8848U, true}',
         'army2 lime/olive package identity drifted')
    need(catalog, '{"Teal green",     "Ilurer",    0x00CCCCU, 0x4E7868U, true}',
         'army4 teal-green package identity drifted')
    need(catalog, '{"Yellow / gold",  "Albor",     0xCC6600U, 0x987E3DU, true}',
         'army5 yellow/gold package identity drifted')
    need(catalog, '{"Gray (special)", "Golrathen", 0x333333U, 0x888888U, false}',
         'army8 special gray identity drifted')
    if 'Faction 1 — Blue' in editor or 'Faction 2 — Red' in editor:
        raise RuntimeError('fixed faction-number colour labels remain in editor UI')
    if 'placement.faction_index%6' in editor or 'placement.faction_index%5' in editor:
        raise RuntimeError('synthetic faction-number marker colours remain')
    need(editor, 'graphics_family_ui_swatch(graphics_family_for_faction(placement.faction_index)',
         'placement marker swatches no longer follow LEV-resolved graphics family')
    print('V545_FACTION_GRAPHICS_FAMILY_SOURCE_CONTRACT PASS')
    return 0

if __name__ == '__main__':
    try: sys.exit(main())
    except Exception as e:
        print('V545_FACTION_GRAPHICS_FAMILY_SOURCE_CONTRACT FAIL:', e, file=sys.stderr); sys.exit(1)
