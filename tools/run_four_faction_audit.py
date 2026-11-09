#!/usr/bin/env python3
"""30-minute four-AI stock-PCK Lab audit; preserves 20 Hz simulation, samples at 1 Hz.
CPU share is relative to one logical CPU; headless replay uses a single thread.
This cannot launch the native Windows game or establish native whole-game parity.
"""
from pathlib import Path
import argparse,os,subprocess,json,hashlib,time,csv

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('pck_root',type=Path);p.add_argument('build',type=Path)
    p.add_argument('--map',default='mittelpunkt');p.add_argument('--minutes',type=int,default=30)
    p.add_argument('--seed',type=int,default=12345);p.add_argument('--cpu-share',type=float,default=.5)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--repeat',type=int,default=1)
    a=p.parse_args()
    if not 0<a.cpu_share<=1 or a.minutes<1 or a.repeat<1:p.error('minutes/repeat must be positive and CPU share must be in (0,1]')
    root=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    binary=out/'faction_replay';cmd=['g++','-std=c++20','-O2','-I'+str(root/'include'),'-I'+str(root/'tools'),str(root/'tools/simlab_stock_replay.cpp'),str(a.build.resolve()/'libthandor_map_core.a'),'-pthread','-ldl','-o',str(binary)]
    c=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(c.stdout+c.stderr)
    if c.returncode:raise RuntimeError(c.stderr)
    binary.chmod(binary.stat().st_mode|0o111)
    metadata={'schema':'thandor-four-faction-audit-v1','map':a.map,'seed':a.seed,'factions':4,'control':'all AI; human mask=0, player counts=0','ticks_per_second':20,'ticks':a.minutes*60*20,'simulated_minutes':a.minutes,'sample_hz':1,'event_observation_hz':20,'callback_step_ticks':1,'cpu_share_of_one_cpu':a.cpu_share,'threads':1,'priority':'nice +10','funds':'authored LEV, no override','fast_build':False,'repeat_count':a.repeat,'native_full_game_executed':False,'runs':[]}
    for rep in range(a.repeat):
        target=out/f'run_{rep+1}';target.mkdir(exist_ok=True)
        if any(target.glob('*.csv')) or any(target.glob('*.partial')):raise RuntimeError(f'Refusing to overwrite an existing audit: {target}')
        env=dict(os.environ,THANDOR_SIMLAB_FACTION_AUDIT_DIR=str(target),THANDOR_SIMLAB_AUDIT_SEED=str(a.seed),THANDOR_SIMLAB_AUDIT_CPU_SHARE=str(a.cpu_share),THANDOR_SIMLAB_REPLAY_PHASES='1')
        start=time.monotonic();command=['nice','-n','10',str(binary),str(a.pck_root.resolve()),a.map,str(metadata['ticks']),'1']
        proc=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
        lines=[]
        for line in proc.stdout:
            lines.append(line)
            if line.startswith(('AUDIT_PROGRESS','REPLAY_PHASE','ELAPSED_SECONDS')):print(f'run={rep+1} '+line.rstrip(),flush=True)
        code=proc.wait();(target/'replay.log').write_text(''.join(lines))
        if code:raise RuntimeError(f'Audit exited {code}; see {target}/replay.log')
        with (target/'state_hashes.csv').open() as stream: hashes=list(csv.DictReader(stream))
        expected={int(row['tick']):int(row['entities']) for row in hashes}
        from collections import Counter
        counts=Counter()
        with (target/'entities.csv').open() as stream:
            for row in csv.DictReader(stream):
                if None in row or any(v is None for v in row.values()):raise RuntimeError('Truncated entity trace; simulation hashes must be checked separately')
                counts[int(row['tick'])]+=1
        if dict(counts)!=expected:raise RuntimeError('Entity trace coverage does not match state samples')
        records={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(target.glob('*.csv'))}
        with (target/'state_hashes.csv').open() as stream:last=list(csv.DictReader(stream))[-1]
        if int(last['tick'])!=metadata['ticks']:raise RuntimeError('Replay ended before requested duration')
        metadata['runs'].append({'directory':target.name,'wall_seconds':round(time.monotonic()-start,3),'final_tick':int(last['tick']),'final_rolling_logical_hash':last['rolling_logical_hash'],'csv_sha256':records})
        (out/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
    if len(metadata['runs'])>1:
        first=metadata['runs'][0]['csv_sha256'];metadata['same_engine_repeat_identical']=all(r['csv_sha256']==first for r in metadata['runs'][1:])
        (out/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
        if not metadata['same_engine_repeat_identical']:raise RuntimeError('Same-engine audit differs; inspect CSV traces')
    print(out/'manifest.json',flush=True)
if __name__=='__main__':main()
