#!/usr/bin/env python3
"""Validate the pinned Mittelpunkt/seed12345 audit and render its reviewed findings.
Usage: analyze_four_faction_audit.py AUDIT_DIR [--terrain terrain.csv]
"""
from pathlib import Path
import argparse,csv,collections,json,math

def read(path):
    with path.open(newline='') as stream:rows=list(csv.DictReader(stream))
    if any(None in r or any(v is None for v in r.values()) for r in rows):raise ValueError('Incomplete CSV: '+str(path))
    return rows

def write(path,rows):
    if not rows:return
    with path.open('w',newline='') as stream:
        w=csv.DictWriter(stream,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)

def clock(tick):
    seconds=int(tick)/20;return f'{int(seconds//60):02d}:{seconds%60:05.2f}'

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('audit_dir',type=Path);p.add_argument('--terrain',type=Path);a=p.parse_args()
    out=a.audit_dir.resolve();run=out/'run_1';metadata=json.loads((out/'manifest.json').read_text())
    factions=read(run/'factions.csv');events=read(run/'events.csv');entities=read(run/'entities.csv');metrics=read(run/'metrics.csv');purchase=read(run/'purchases.csv');orders=read(run/'military_orders.csv')
    assets={int(x['arm']):x for x in read(run/'assets.csv')};tech={int(x['technology']):x for x in read(run/'technologies.csv')};names={int(x['faction']):x['name'] for x in read(run/'faction_names.csv')}
    end=metadata['ticks'];expected=list(range(0,end+1,20))
    for f in range(1,5):
        if [int(x['tick']) for x in factions if int(x['faction'])==f]!=expected:raise ValueError('Missing/duplicated faction sample')
    hashes=read(run/'state_hashes.csv')
    if metadata['map']!='mittelpunkt' or metadata['seed']!=12345 or end!=36000 or hashes[-1]['rolling_logical_hash']!='1a4ae53e4c01feb2':
        raise ValueError('This reviewed narrative is pinned to Mittelpunkt/12345 and the recorded final hash; review findings before adapting it to a changed game')
    if [int(x['tick']) for x in hashes]!=expected:raise ValueError('Missing state hash sample')
    if len({int(x['sequence']) for x in orders})!=len(orders):raise ValueError('Repeated military command records')
    if orders and [int(x['sequence']) for x in orders]!=list(range(1,int(orders[-1]['sequence'])+1)):raise ValueError('Military trace overrun/gap')
    result={'map':metadata['map'],'seed':metadata['seed'],'ticks':end,'minutes':end/1200,'faction_samples':len(factions),'entity_samples':len(entities),'military_orders':len(orders),'events':len(events),'native_full_game_compared':False,'same_engine_repeat_identical':metadata.get('same_engine_repeat_identical'),'factions':[]}
    milestones={'initial','spawn','construction_complete','production_start','producer_release','research_queued','research_start','research_complete','terminal','ownership_changed','lifecycle','attack_target','weapon_volley','stationary_10s','stationary_end'}
    research=[]
    for x in events:
        if x['event']=='research_complete':
            tec=int(x['value']);research.append({'tick':x['tick'],'time':clock(x['tick']),'faction':x['faction'],'technology':tec,'title':tech.get(tec,{}).get('title','')})
    write(out/'research_completed.csv',research)
    trajectories=collections.defaultdict(list)
    for x in entities:trajectories[int(x['entity_id'])].append(x)
    movement=[]
    for entity_id,trace in trajectories.items():
        if trace[0]['building']=='1':continue
        distance=sum(math.hypot(int(b['x_q12'])-int(c['x_q12']),int(b['y_q12'])-int(c['y_q12']))/4096 for b,c in zip(trace,trace[1:]))
        first,last=trace[0],trace[-1];movement.append({'entity_id':entity_id,'arm':first['arm'],'name':assets.get(int(first['arm']),{}).get('name',''),'initial_faction':first['faction'],'final_faction':last['faction'],'first_sample':clock(first['tick']),'terminal':last['terminal'],'removed':last['removed'],'sampled_path_world':round(distance,6),'volleys':last['volleys'],'end_x_q12':last['x_q12'],'end_y_q12':last['y_q12'],'end_goal_x_q12':last['move_target_x_q12'],'end_goal_y_q12':last['move_target_y_q12'],'stationary_seconds_with_goal':int(last['stationary_ticks'])/20})
    write(out/'unit_movement_summary.csv',movement)
    for f in range(1,5):
        samples=[x for x in factions if int(x['faction'])==f];final=samples[-1];ev=[x for x in events if int(x['faction'])==f]
        builds=collections.Counter(int(x['arm']) for x in ev if x['event']=='construction_complete');completed=[x for x in research if int(x['faction'])==f]
        decisions=collections.Counter(int(x['decision']) for x in purchase if int(x['faction'])==f)
        result['factions'].append({'faction':f,'name':names[f],'final':final,'new_assets':dict(builds),'research_jobs':len(completed),'distinct_research_ids':len({x['technology'] for x in completed}),'purchase_decisions':dict(decisions)})
        timeline=[]
        for x in ev:
            if x['event'] not in milestones:continue
            row=dict(x);row['time']=clock(x['tick']);row['asset_name']=assets.get(int(x['arm']),{}).get('name','')
            row['technology_title']=tech.get(int(x['value']),{}).get('title','') if x['event'].startswith('research_') else ''
            # Move target event Y is represented as a uint64 wire word in raw events.csv.
            timeline.append(row)
        write(out/f'faction_{f}_timeline.csv',timeline)
    if sum(x['research_jobs'] for x in result['factions'])!=int(metrics[-1]['research_completed']):raise ValueError('Per-faction completions disagree with core total')
    if sum(int(x['final']['volleys']) for x in result['factions'])!=int(metrics[-1]['shots']):raise ValueError('Volley/shot distinction requires review in this fixture')
    result['global_final']=metrics[-1]
    (out/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    report=['# Four-faction game audit — 30 simulated minutes','',f"Map: **{metadata['map']}**. Seed: **{metadata['seed']}**. All four factions AI-controlled; original LEV funds, stock KI and technologies, no fast-build or cash override. Simulation: 20 Hz / 36,000 ticks. Summary and position samples: 1 Hz; events and command records observed every tick. One thread, nice +10, target 50% of one logical CPU.",'','This is a complete-duration headless Simulation Lab run. The native Windows game did not run. Same-engine repetition tests determinism, not native parity. Native-source arithmetic comparisons are reported separately.','', '| Faction | New buildings / units | Live buildings / units at 30:00 | Research jobs (distinct IDs) | Movement, world units | Volleys | State |','| --- | ---: | ---: | ---: | ---: | ---: | --- |']
    for f in result['factions']:
        s=f['final'];new_b=sum(n for arm,n in f['new_assets'].items() if assets.get(arm,{}).get('building')=='1');new_u=sum(f['new_assets'].values())-new_b
        report.append(f"| {f['faction']} — {f['name']} | {new_b} / {new_u} | {s['buildings']} / {s['units']} | {f['research_jobs']} ({f['distinct_research_ids']}) | {float(s['movement_world']):.2f} | {s['volleys']} | {'Inactive after transfer' if s['lifecycle']=='0' else 'Active'} |")
    report+=['','New assets count completions during the game; starting units/buildings are excluded. Ownership transfers are not builds or kills. Research jobs can repeat an already unlocked ID when another factory has a retained job; distinct IDs are shown separately. Movement sums every tick and is credited to the owner at that tick. Volleys and projectile count happen to both equal one in this game; they are different measures in general.','','## Findings','','- Only **one projectile** fired, at tick 3770 (03:08.50), by faction 3’s original Weasel at faction 1’s Weasel. Core projectile damage totaled **67**. The other observed health losses are not evidence of additional combat: the core records 13,875 water-damage integrity loss and 1,520 water-damage applications. Net HP losses/gains also include regeneration and upgrades.','- Faction 1’s light weapons factory remains in TEC 4 at **7/140** elapsed ticks at 30:00, with root flags **0x41** (research plus unpowered) and 240 Q4 unpowered demand. It has 68,000 Q4 Xenite but no fuel extraction. Research began at 01:29.25.','- Faction 2 ends with **3,200 Q4 Xenite**, no extractor, no income and an unplaced primary-queue item. It built a light factory and Ion laser lab but no replacement units. Its original Weasel moved only 4.38 world units despite 562 military command records.','- Faction 3 transfers five live buildings to faction 4 at **09:02.50** (tick 10850), then becomes inactive. Those buildings were not destroyed. Both of its units had already reached terminal state.','- Faction 4 builds ten power plants, both walker/tracked factories and two research labs, completes 62 research jobs, and absorbs faction 3. It still fires no shots. Its surviving mobile inventory is four ARM1 Weasels and one ARM30 unit; constructing a Walker factory did not produce a walker in this run.','','## Reproduced movement problems','','| Audit entity | ARM | Position Q12 at 30:00 | Requested target Q12 | Stationary with goal |','| --- | --- | --- | --- | ---: |']
    for x in movement:
        if x['stationary_seconds_with_goal']>=10:report.append(f"| {x['entity_id']} | {x['arm']} | {x['end_x_q12']}, {x['end_y_q12']} | {x['end_goal_x_q12']}, {x['end_goal_y_q12']} | {x['stationary_seconds_with_goal']:.2f} s |")
    report+=['','The core recorded **12,105 rejected movement steps** and 15,720 routed solves. Entities 35 and 46 are in crowded base traffic; entity 39 has no nearby live army footprint in the final snapshot. A separate frozen-state probe compares the Lab fast A* adapter with the editor’s recovered FIFO resolver:','', '- Entity 35: both return a route and the same first waypoint.','- Entity 46: both return a route and the same first waypoint.','- **Entity 39: fast adapter returns blocked; recovered FIFO returns routed**, first waypoint (175324, -219641). This reproduces a difference between the two editor route implementations on actual game inputs. The FIFO helper is not a running native game and its returned route is not proof that all physical steps will succeed.','', 'See `route_comparison.log` and `tools/probe_audit_stalled_routes.cpp` in the source package. Changing every route to FIFO would be a broad, expensive behavior change; this delivery records the failing state without claiming an unverified collision/path fix.','','## Native-source comparisons on this game’s values','','The unchanged native power-candidate and planning-capacity functions were compiled against the **7,204 recorded one-second faction snapshots**. Their arithmetic matched the editor helper: 7,204 power checks and **28,816 capacity checks**. A shared seed of 12345 was used for each isolated weighted-candidate/RNG comparison. There were 564 admitted power candidates under the controlled gates, and 5,513 snapshots with equal fuel-backed supply and generation capacity (no power proposal). Four tick-zero zero-capacity images were excluded through the native no-ARM330 gate to avoid inventing a divide-by-zero situation.','','This isolates numerical behavior: ARM330 presence and the no-pending-ARM310 gate were controlled, so candidate admission from the real world/workspaces is not established by these checks. Native purchase source also confirms the local budget subtraction, stop at the first unaffordable candidate, and retained failed cache. No arbitrary cache timeout was added.','','Full native build/research/attack timelines, RNG call order, diplomacy and first-divergence state comparison remain unverified. They require a native executable/runtime and matching complete initial world state.','','## Repetition and recording integrity','','Both games reached tick 36000. All 1,801 one-second rolling state-hash records and all 11 other complete CSV tables match byte for byte. The second run’s auxiliary entities.csv is truncated mid-row at tick 35400; its available bytes exactly match the first run’s prefix. Thus the simulation/action traces agree, but two complete entity-position recordings were not obtained. The first run has all 75,273 entity samples through 30:00. The recorder now uses checked flush/close, atomic final filenames and row-count validation; two short repeated capture runs validate that change. See manifest.json for the preserved capture error.','','## Per-faction detail']
    for f in result['factions']:
        idx=f['faction'];report+=['',f"### {idx} — {f['name']}",'', '**New assets completed:** '+('; '.join(f"{count}× {assets[arm]['name']} (ARM{arm})" for arm,count in sorted(f['new_assets'].items())) or 'none')+'.','', '| First completion | Technology | Name |','| --- | --- | --- |']
        seen=set()
        for x in research:
            if int(x['faction'])!=idx or x['technology'] in seen:continue
            seen.add(x['technology']);report.append(f"| {x['time']} | TEC{x['technology']} | {x['title'].replace('|','/')} |")
        report+=['',f"Complete construction, production, research, attack and loss timeline: [faction_{idx}_timeline.csv](faction_{idx}_timeline.csv)."]
    report+=['','## Files and limits','','- `run_1/` and `run_2/`: original CSV tables, one-second positions/resources/state summaries, every-tick observed action events, candidate scores/reasons, purchases, military orders and logical state hashes.','- `research_completed.csv`: all 70 completed research jobs, in time order, with PCK titles.','- `unit_movement_summary.csv`: every mobile entity’s sampled path, end goal, shots and stalled duration. Per-unit path lengths use 1 Hz samples; per-faction totals use every tick.','- `analysis.json`: machine-readable totals and decision counts. `manifest.json`: inputs, run times, checksums and repeat result.','- Entity IDs are observer-local. They follow (original placement, ARM, construction start tick) plus stable occurrence order. This map has no pioneer conversion; native process IDs are not comparable.','- `root_hp_loss` events are net root-integrity changes, not attributed projectile damage. `technology_unlocked` can come from creation, exchange or scripts; only `research_complete` is counted as a completed research job.','- An order can be issued repeatedly or choose the current position; order counts do not prove distance traveled or successful combat.','- Headless stock replay uses the shared core/runtime catalog but does not run ImGui, the renderer, a native Windows executable or every GUI presentation input. The exact native diplomacy matrix is not recorded; ownership/lifecycle transitions are.','- No whole-game native parity or GPU/visual correctness is claimed.']
    if a.terrain:
        import matplotlib;matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        import numpy as np
        colors={1:'#3174c8',2:'#7b9b23',3:'#cf4646',4:'#218c84'}
        fig,axes=plt.subplots(2,2,figsize=(12,8),layout='constrained')
        for f in range(1,5):
            rows=[x for x in factions if int(x['faction'])==f];t=[int(x['tick'])/1200 for x in rows]
            for axis,key,scale,title in [(axes[0,0],'buildings',1,'Live buildings'),(axes[0,1],'research_completions',1,'Completed research jobs'),(axes[1,0],'movement_world',1,'Distance moved (world units)'),(axes[1,1],'energy_unpowered_q4',16,'Unpowered energy demand')]:
                axis.plot(t,[float(x[key])/scale for x in rows],color=colors[f],label=names[f],lw=1.8);axis.set_title(title);axis.set_xlabel('Simulated minutes');axis.grid(alpha=.2)
        axes[0,0].legend(frameon=False);fig.suptitle('Mittelpunkt · four AI factions · seed 12345 · 30 simulated minutes',fontsize=14);fig.savefig(out/'faction_comparison.png',dpi=160);plt.close(fig)
        terrain=read(a.terrain);x=np.array([float(v['x']) for v in terrain]);y=np.array([float(v['y']) for v in terrain]);z=np.array([float(v['height']) for v in terrain]);water=np.array([float(v['water_delta'])>0 for v in terrain])
        fig,axes=plt.subplots(2,2,figsize=(12,10),layout='constrained')
        for f,axis in zip(range(1,5),axes.flat):
            axis.scatter(x,y,c=z,cmap='Greys',s=1.5,alpha=.32,rasterized=True);axis.scatter(x[water],y[water],s=2,color='#86c5da',alpha=.7,rasterized=True)
            for entity_id,trace in trajectories.items():
                if trace[0]['building']=='1':continue
                points=[v for v in trace if int(v['faction'])==f and v['removed']=='0']
                if not points:continue
                axis.plot([int(v['x_q12'])/4096 for v in points],[int(v['y_q12'])/4096 for v in points],color=colors[f],alpha=.8,lw=1)
                last=points[-1];axis.scatter(int(last['x_q12'])/4096,int(last['y_q12'])/4096,marker='x' if last['terminal']=='1' else 'o',color=colors[f],s=25)
                if int(last['stationary_ticks'])>=200:axis.annotate(str(entity_id),(int(last['x_q12'])/4096,int(last['y_q12'])/4096),xytext=({35:(8,-12),46:(10,12)}.get(entity_id,(4,4))),textcoords='offset points',fontsize=8)
            axis.set_title(names[f]);axis.set_aspect('equal');axis.set_xlabel('World X');axis.set_ylabel('World Y')
        fig.suptitle('Mobile unit tracks · water shown in blue · crosses: terminal units · labels: stalled IDs',fontsize=12);fig.savefig(out/'unit_tracks.png',dpi=150);plt.close(fig)
        report[4:4]=['![Faction comparison](faction_comparison.png)','', '![Mobile unit tracks](unit_tracks.png)','']
    (out/'REPORT.md').write_text('\n'.join(report)+'\n')
    print(json.dumps({'report':str(out/'REPORT.md'),'faction_samples':len(factions),'entity_samples':len(entities),'events':len(events),'military_orders':len(orders)}))
if __name__=='__main__':main()
