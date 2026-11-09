#!/usr/bin/env python3
"""V702: graphics family material lookup is O(1) for repeated use in each scene."""
from pathlib import Path
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
root = parser.parse_args().source
app = (root / 'src/editor/editor_app.cpp').read_text()
header = (root / 'src/editor/editor_app.hpp').read_text()
perf = (root / 'src/editor/editor_performance_log.hpp').read_text()
start = app.index('    // V702: A dynamic-scene build visits many actors')
end = app.index('    const auto layer_for_key=', start)
block = app[start:end]
assert 'std::array<const ArmyTextureFamily*,9> scene_army_families{}' in block
assert 'const auto* cached=scene_army_families[clamped]' in block
assert block.index('scene_army_families[clamped]') < block.index('const std::string key=terrain_base_path(army_base)')
assert 'scene_army_families[clamped]=found->second.get()' in block
assert block.index('if(scene_effect_family)') < block.index('const std::string key=effect_base+"#effect"')
assert 'scene_effect_family=found->second.get()' in block
assert 'dynamic_texture_family_fast_hits_+=scene_family_fast_hits' in app
assert 'dynamic_texture_family_fast_misses_+=scene_family_fast_misses' in app
assert 'dynamic_texture_family_fast_hits_' in header
assert 'dynamic_texture_family_fast_hits=' in perf
assert 'dynamic_texture_family_fast_misses=' in perf
assert 'kEditorVersion' in (root / 'include/thandor/editor_version.hpp').read_text()
print('V702 scene-local family lookup source contract PASS')
