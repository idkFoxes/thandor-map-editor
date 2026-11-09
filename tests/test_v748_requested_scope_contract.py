#!/usr/bin/env python3
from pathlib import Path
import re, sys
ROOT=Path(__file__).resolve().parents[1]

def text(rel):
    p=ROOT/rel
    assert p.exists(), f"missing {rel}"
    return p.read_text(encoding='utf-8',errors='replace')

def find(suffix):
    hits=list(ROOT.rglob(suffix))
    assert hits, f"missing {suffix}"
    return hits[0].read_text(encoding='utf-8',errors='replace')

main=find('src/editor/main.cpp')
assert 'V748' in main, 'executable version path is not V748'
combat=find('src/gameplay/ai/combat.cpp')
assert 'AiCombatTarget_GetClassBaseScore' in combat
assert 'classIndex >= 24' in combat
workspaces=find('include/thandor/gameplay/ai/workspaces.h')
assert re.search(r'AI_WORKSPACE01_CAPACITY\s*=\s*64',workspaces)
assert re.search(r'AI_WORKSPACE05_CAPACITY\s*=\s*32',workspaces)
assert re.search(r'AI_WORKSPACE12_CAPACITY\s*=\s*32',workspaces)
assert re.search(r'AI_CANDIDATE_WORKSPACE_CAPACITY\s*=\s*128',workspaces)
route=find('src/world/pathing/route.cpp')
assert re.search(r'ENTITY_PATHING_PRIORITY_PAIR_CAPACITY\s*=\s*32',route)
assert 'EntityPathing_RebuildOverlappingGroupRoutes' in route
units=find('src/gameplay/ai/units.cpp')
assert 'AiUnitCommand_AssignWorkspacePoint' in units and 'AiUnitCommand_AssignFactionAnchorPoint' in units
factory=find('src/gameplay/army/factory.cpp')
assert 'FixedMath_Atan2Angle16' in factory
construction=find('src/gameplay/ai/construction.cpp')
assert 'triangle0NormalAngles' in construction
assert 'AiConstruction_PlacementHeadingAngle16' in construction
technology=find('src/gameplay/ai/technology.cpp')
assert 'g_AiWorkspace12TechnologyCandidates' in technology
assert 'AiTechnologyCandidate_AddBestResearch' in technology
# Requested combat-adjacent systems remain represented and testable in the source tree.
for needle in ['aircraft','projectile','destruction','effect']:
    assert any(needle in str(p).lower() for p in ROOT.rglob('*') if p.is_file()), f'no {needle} source/tests found'
print('V748 requested-scope source contract: PASS')
