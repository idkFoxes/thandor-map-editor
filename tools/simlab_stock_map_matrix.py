#!/usr/bin/env python3
"""Repeat a deterministic, stock-PCK-backed Simulation Lab benchmark.

This is the Simulation Lab, not a retail replay or a Vulkan pixel test.
The executable is an explicit input to avoid comparing different binaries.
"""
import argparse
import json
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('executable', type=Path)
    ap.add_argument('pck_root', type=Path)
    ap.add_argument('--maps', nargs='+', default=['asgard', 'niflheim', 'ignis'])
    ap.add_argument('--ticks', type=int, default=1000)
    ap.add_argument('--runs', type=int, default=1)
    ap.add_argument('--repetitions', type=int, default=3)
    ap.add_argument('--timeout', type=int, default=240)
    ap.add_argument('--output', type=Path)
    args=ap.parse_args()
    if args.ticks<1 or args.runs<1 or args.repetitions<1:
        ap.error('ticks, runs and repetitions must be positive')
    executable=args.executable.resolve()
    pck=args.pck_root.resolve()
    if not executable.is_file():ap.error(f'executable not found: {executable}')
    if not (pck/'LEVEL.PCK').is_file():ap.error(f'LEVEL.PCK not found under {pck}')
    report={'schema':'thandor-stock-map-matrix-v1','executable':str(executable),
            'pck_root_in_report':'[redacted - supplied path]',
            'ticks':args.ticks,'runs':args.runs,'repetitions':args.repetitions,
            'maps':[],'comparison_note':'This runs only the supplied executable; comparing versions requires same hardware, arguments, and release flags.'}
    all_ok=True
    for name in args.maps:
        times=[]; results=[]
        for repeat in range(args.repetitions):
            cmd=[str(executable), str(pck), name, str(args.ticks), str(args.runs)]
            t0=time.perf_counter()
            try:
                result=subprocess.run(cmd, capture_output=True, text=True,timeout=args.timeout,check=False)
                seconds=time.perf_counter()-t0
                metrics=[dict(re.findall(r'([a-z][a-z_]+)=([^ ]+)', line))
                         for line in result.stdout.splitlines() if line.startswith('RUN index=')]
                if result.returncode or len(metrics)!=args.runs:
                    all_ok=False
                    results.append({'repeat':repeat,'seconds':round(seconds,4),'exit_code':result.returncode,
                                    'error':(result.stderr or result.stdout)[-1200:]})
                else:
                    times.append(seconds)
                    results.append({'repeat':repeat,'seconds':round(seconds,4),'run_metrics':metrics})
            except subprocess.TimeoutExpired:
                all_ok=False
                results.append({'repeat':repeat,'error':f'timeout after {args.timeout}s'})
        row={'map':name,'repeat_results':results,
             'median_seconds':round(statistics.median(times),4) if times else None,
             'range_seconds':[round(min(times),4),round(max(times),4)] if times else None}
        report['maps'].append(row)
        # Preserve completed rows even if a later, expensive stock map times out.
        if args.output:
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
        print(name, 'median_seconds=',row['median_seconds'],'passes=',len(times),'/',args.repetitions,flush=True)
    data=json.dumps(report,indent=2,sort_keys=True)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(data)
    else:print(data)
    return 0 if all_ok else 1

if __name__=='__main__':sys.exit(main())
