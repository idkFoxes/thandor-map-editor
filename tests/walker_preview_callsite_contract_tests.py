#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def argument_count(source: str, open_paren: int) -> tuple[int, int]:
    depth = 0
    commas = 0
    in_string: str | None = None
    escaped = False
    index = open_paren
    saw_token = False
    while index < len(source):
        ch = source[index]
        if in_string is not None:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == in_string:
                in_string = None
            index += 1
            continue
        if ch in ('"', "'"):
            in_string = ch
        elif ch == '(':
            depth += 1
            if depth > 1:
                saw_token = True
        elif ch == ')':
            depth -= 1
            if depth == 0:
                return (commas + 1 if saw_token else 0), index
        elif ch == ',' and depth == 1:
            commas += 1
        elif depth == 1 and not ch.isspace():
            saw_token = True
        index += 1
    raise SystemExit('unterminated apply_walker_pose_to_instances call')


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    args = parser.parse_args()
    source_root = args.source.resolve()
    text = (source_root / 'src/editor/editor_app.cpp').read_text(encoding='utf-8')

    signature = '''bool apply_walker_pose_to_instances(const thandor::ArmyAssetPreset& preset,
                                    std::span<const thandor::ModelVisualNode> hierarchy,
                                    std::vector<ModelHierarchyInstance>& instances,
                                    thandor::SimulationActorState* actor,
                                    TerrainHeightFn&& terrain_height)'''
    require(signature in text, 'walker pose helper signature drifted from the five-argument contract')
    require('instances,0U,false,' not in text,
            'obsolete six-argument walker preview call remains')
    require('instances,nullptr,\n        [](float,float){return 0.0F;});' in text,
            'asset preview does not pass the null runtime-state pointer')

    marker = 'apply_walker_pose_to_instances('
    starts: list[int] = []
    offset = 0
    while True:
        found = text.find(marker, offset)
        if found < 0:
            break
        starts.append(found)
        offset = found + len(marker)
    # One definition and three calls. Only the calls are checked by argument arity.
    require(len(starts) == 4, f'unexpected walker helper occurrence count: {len(starts)}')
    arities = []
    for start in starts[1:]:
        count, _ = argument_count(text, start + len('apply_walker_pose_to_instances'))
        arities.append(count)
    require(arities == [5, 5, 5], f'walker helper callsite arities are {arities}, expected [5, 5, 5]')

    print('walker preview/helper callsite contract PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
