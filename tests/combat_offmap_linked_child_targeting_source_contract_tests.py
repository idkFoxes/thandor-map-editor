#!/usr/bin/env python3
from pathlib import Path
import argparse

ap=argparse.ArgumentParser(); ap.add_argument('--source', default='.'); args=ap.parse_args()
root=Path(args.source)
app=(root/'src/editor/editor_app.cpp').read_text()

begin=app.find('const auto choose_state5_candidate=')
end=app.find('struct State5EmitterPose',begin)
if begin < 0 or end < 0:
    raise SystemExit('V552_OFFMAP_LINKED_CHILD_TARGETING_SOURCE_CONTRACT FAIL: candidate helper not found')
section=app[begin:end]

checks={
    'state5 candidate scan keeps authored destruction exclusion':
        'building_destruction_states_.find(index)' in section and 'if(lifetime==0U||age>=lifetime)continue;' in section,
    'state5 linked-child scan excludes source and offmap/invisible children':
        'if(id==source_serial||!entity.visible||(entity.maximum_integrity>0&&entity.integrity<=0))continue;' in section,
    'state5 linked-child scan still uses transient identity':
        'SimulationTransientRef{SimulationTransientKind::LinkedClass21,id}' in section,
    'shared collision raycast uses the same linked-child live-surface boundary':
        'for(auto& [serial,entity]:simulation_linked_child_entities_)' in app and
        'if(!entity.visible||(entity.maximum_integrity>0&&entity.integrity<=0))continue;' in app,
    'mission lifecycle explicitly leaves and re-enters the live model surface':
        'child.visible=false;' in app and 'child.visible=true;' in app,
}
failed=[name for name,ok in checks.items() if not ok]
if failed:
    raise SystemExit('V552_OFFMAP_LINKED_CHILD_TARGETING_SOURCE_CONTRACT FAIL: '+', '.join(failed))
print('V552_OFFMAP_LINKED_CHILD_TARGETING_SOURCE_CONTRACT PASS')
