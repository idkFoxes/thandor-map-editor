#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path


def need(text: str, token: str, message: str) -> None:
    if token not in text:
        raise RuntimeError(f"{message}: missing {token}")


def main() -> int:
    ap=argparse.ArgumentParser(); ap.add_argument('--source',type=Path,required=True); args=ap.parse_args()
    root=args.source
    catalog=(root/'src/core/catalog.cpp').read_text()
    editor=(root/'src/editor/editor_app.cpp').read_text()
    need(catalog, '{"Lime / olive",   "Thoraner",  0x00DD00U, 0x7A8848U, true}', 'army2 identity')
    need(catalog, '{"Teal green",     "Ilurer",    0x00CCCCU, 0x4E7868U, true}', 'army4 identity')
    need(catalog, '{"Yellow / gold",  "Albor",     0xCC6600U, 0x987E3DU, true}', 'army5 identity')
    need(catalog, '{"Gray (special)", "Golrathen", 0x333333U, 0x888888U, false}', 'army8 identity')
    need(editor, 'graphics_family_representative_rgb24(graphics_family)', 'centralized editor swatch')
    need(editor, 'Golrathen (gray army8) is a graphics family, not gameplay faction 8.',
         'special-family UI explanation')
    if 'army2 yellow' in editor.lower() or 'army5 orange' in editor.lower():
        raise RuntimeError('stale family-name assumptions remain in editor')
    print('V574_CP13_FACTION_FAMILY_COLOR_SOURCE_CONTRACT PASS')
    return 0


if __name__=='__main__':
    try: raise SystemExit(main())
    except Exception as exc:
        print('V574_CP13_FACTION_FAMILY_COLOR_SOURCE_CONTRACT FAIL:',exc)
        raise SystemExit(1)
