#!/usr/bin/env python3
"""Run the existing core replay with declared AI comparison inputs.
Human faction 1; other active factions are AI. Seed 12345; no cash override.
The ordinary one-tick callback cadence is retained. Native's speed-five hash
runner is not reproduced by the Lab's partial callback multiplier.
This reports core behavior; it cannot compare native full-game state hashes.
"""
from pathlib import Path
import argparse,subprocess,tempfile
p=argparse.ArgumentParser()
p.add_argument('pck_root',type=Path)
p.add_argument('build',type=Path)
p.add_argument('--map',default='stromschnelle')
p.add_argument('--ticks',type=int,default=1400)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1];build=a.build.resolve()
s=(root/'tools/simlab_stock_replay.cpp').read_text()
for before,after in [
 ('scenario.simulate_all_active_factions_as_ai = true;','scenario.simulate_all_active_factions_as_ai = false;\n    scenario.human_faction_mask=2U;'),
 ('config.base_seed = 1414024772U;','config.base_seed = 12345U;'),
 ('config.vary_seed_per_run = true;','config.vary_seed_per_run = false;')]:
 if s.count(before)!=1:raise SystemExit('Replay setup changed; review comparison input: '+before)
 s=s.replace(before,after)
with tempfile.TemporaryDirectory(prefix='thandor-faction-ai-replay-') as tmp:
 source=Path(tmp)/'replay.cpp';exe=Path(tmp)/'replay';source.write_text(s)
 subprocess.run(['g++','-std=c++20','-O2','-I'+str(root/'include'),'-I'+str(root/'tools'),str(source),str(build/'libthandor_map_core.a'),'-pthread','-ldl','-o',str(exe)],check=True)
 with a.output.open('w') as out:
  out.write('COMPARISON_INPUT human_faction=1 AI=other_active seed=12345 callback_step=1 funds=LEV\n');out.flush()
  subprocess.run([str(exe),str(a.pck_root.resolve()),a.map,str(a.ticks),'1'],stdout=out,stderr=subprocess.STDOUT,check=True)
print(a.output)
