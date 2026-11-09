#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    args = parser.parse_args()
    editor = (args.source / 'src/editor/editor_app.cpp').read_text()
    catalog = (args.source / 'src/core/catalog.cpp').read_text()
    if '133,110,91' in editor or 'warm/brown representative' in editor:
        raise RuntimeError('superseded CP12 whole-image army8 brown inference remains in editor')
    if 'graphics_family_representative_rgb24(graphics_family)' not in editor:
        raise RuntimeError('editor swatches are no longer centralized on core family semantics')
    if '{"Gray (special)", "Golrathen", 0x333333U, 0x888888U, false}' not in catalog:
        raise RuntimeError('army8 is not pinned as the package-visible special gray family')
    print('CP12_FAMILY8_UI_SWATCH_SOURCE_CONTRACT PASS (CP13 correction applied)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
