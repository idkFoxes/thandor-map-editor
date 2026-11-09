#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path


def need(text: str, token: str, message: str) -> None:
    if token not in text:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    args = parser.parse_args()
    editor = (args.source / 'src/editor/editor_app.cpp').read_text()

    need(editor, 'bool preload_runtime_resources=false',
         'transient residency preload lost its explicit runtime-resource mode')
    need(editor, 'catalog_.simulation_weapon_profiles_for_hierarchy(hierarchy)',
         'live transient SHT profiles are no longer derived from the resolved hierarchy')
    need(editor, 'preload_effect_textures(node.simulation.timed_effect_id);',
         'live transient land EFF residency was dropped')
    need(editor, 'preload_effect_textures(node.simulation.timed_water_effect_id);',
         'live transient water EFF residency was dropped')
    need(editor, 'preload_effect_textures(node.simulation.damage_effect_id);',
         'live transient damage EFF residency was dropped')
    need(editor, 'preload_shot_textures(weapon.shot_definition_id);',
         'live transient SHT/EFF chain residency was dropped')
    need(editor, '&run.faction_technology_masks[faction],true);',
         'Simulation Lab transient residency no longer uses the selected run technology hierarchy')
    need(editor, 'preload_simulation_lab_preset_textures(*preset,entity.faction_index,nullptr,true);',
         'ordinary produced/linked transient actors no longer request runtime-resource residency')

    produced = 'simulation_produced_entities_[serial]=std::move(entity);'
    linked = 'simulation_linked_child_entities_[child.serial]=std::move(child);'
    for marker, label in ((produced, 'class-13 produced unit'), (linked, 'class-21 linked child')):
        pos = editor.find(marker)
        if pos < 0:
            raise RuntimeError(f'{label} insertion boundary disappeared')
        tail = editor[pos:pos + 600]
        need(tail, 'simulation_static_replacement_dirty_=true;',
             f'{label} can become dynamically drawable before static texture residency rebuild')

    print('CP12_RUNTIME_TEXTURE_RESIDENCY_SOURCE_CONTRACT PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
