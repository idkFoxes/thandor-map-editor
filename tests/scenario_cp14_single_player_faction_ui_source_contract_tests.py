#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path


def need(text: str, token: str, message: str) -> None:
    if token not in text:
        raise RuntimeError(f"{message}: missing {token}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', type=Path, required=True)
    args = ap.parse_args()
    root = args.source
    catalog = (root / 'src/core/catalog.cpp').read_text()
    header = (root / 'include/thandor/catalog.hpp').read_text()
    editor = (root / 'src/editor/editor_app.cpp').read_text()

    expected = [
        ('Ares', '0x0000DDU'),
        ('Thoraner', '0x00DD00U'),
        ('Devoken', '0xDD0000U'),
        ('Ilurer', '0x00CCCCU'),
        ('Albor', '0xCC6600U'),
        ('Urdoner', '0x552200U'),
        ('Alderaden', '0x990099U'),
        ('Golrathen', '0x333333U'),
    ]
    for name, rgb in expected:
        need(catalog, f'"{name}"', f'{name} retail faction name')
        need(catalog, rgb, f'{name} retail text color')

    need(catalog, 'texte\\menue.str resources', 'stock STR provenance')
    need(catalog, '0x2174..0x217B', 'retail task-name resource range')
    need(catalog, 'Ares D00F, Thoraner 0D0F, Devoken 00DF',
         '0x8006 B-G-R-alpha color nibble evidence')
    need(header, 'graphics_family_single_player_text_rgb24', 'frontend text-color API')
    need(editor, 'graphics_family_single_player_text_rgb24(graphics_family)',
         'editor uses retail text color rather than texture swatch')
    need(editor, 'graphics_family_single_player_faction_name(graphics_family)',
         'camera selector uses retail faction names')
    need(editor, 'graphics_family_single_player_faction_name(current_family)',
         'player-slot UI uses retail faction names')
    need(editor, 'const uint32_t current_family=graphics_family_for_faction(static_cast<uint32_t>(faction))',
         'faction heading resolves the effective current family above its controls')
    need(editor, 'readable_editor_text_rgb(', 'dark-editor text contrast adaptation')
    need(editor, ' · Faction ', 'default-font-compatible faction separator')
    need(editor, 'faction_single_player_label(selected_faction_,selected_family)',
         'placement faction combo uses retail faction names')
    need(editor, 'faction_single_player_label(selected_object_faction,selected_object_family)',
         'object faction combo uses retail faction names')
    if 'ColorConvertU32ToFloat4(graphics_family_ui_swatch(graphics_family))' in editor:
        raise RuntimeError('Single Player text color still aliases the package texture swatch')

    print('V574_CP14_SINGLE_PLAYER_FACTION_UI_SOURCE_CONTRACT PASS')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print('V574_CP14_SINGLE_PLAYER_FACTION_UI_SOURCE_CONTRACT FAIL:', exc)
        raise SystemExit(1)
