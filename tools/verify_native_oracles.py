#!/usr/bin/env python3
"""Compile supplied native source bodies as Linux test oracles; no original EXE execution.

Usage: verify_native_oracles.py OPEN_THANDOR_SOURCE EDITOR_BUILD OUTPUT_DIR [PCK_ROOT]
The editor core library must already be built. Native source is an external input.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    if len(sys.argv) not in (4, 5) or not sys.platform.startswith('linux'):
        raise SystemExit(__doc__)
    native, build, output = [Path(p).resolve() for p in sys.argv[1:4]]
    editor = Path(__file__).resolve().parents[1]
    output.mkdir(parents=True, exist_ok=True)
    base = ['g++', '-std=c++20', '-O2', '-fwrapv', '-ffunction-sections', '-fdata-sections',
            '-D__forceinline=inline', '-D__cdecl=', '-D__stdcall=', '-D__fastcall=', '-D__declspec(x)=',
            '-I'+str(editor/'tools/native_oracle_stubs'), '-I'+str(native/'include')]
    records = []

    def run(command, name):
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        (output/(name+'.log')).write_text(result.stdout+result.stderr)
        records.append({'name': name, 'returncode': result.returncode})
        if result.returncode:
            raise RuntimeError(name+' failed: '+(result.stdout+result.stderr)[-4000:])
        return result.stdout

    names = ['core/math/fixed_trig', 'core/math/fixed_transform', 'core/math/fixed_vector',
             'world/terrain/field_sampling', 'world/terrain/occupancy', 'core/math/random']
    sources, objects = [], []
    for name in names:
        source = native/'src'/(name+'.cpp')
        sources.append({'path': 'src/'+name+'.cpp', 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
        obj = output/(source.stem+'.o')
        run(base+['-c', str(source), '-o', str(obj)], 'compile_'+source.stem)
        objects.append(str(obj))
    library = build/'libthandor_map_core.a'
    for label, driver, linked in [
        ('math', 'core_native_math_oracle.cpp', objects),
        ('aircraft', 'aviation_native_aircraft_oracle.cpp', [objects[0]]),
    ]:
        binary = output/('native_'+label+'_oracle')
        extra = []
        if label == 'aircraft':
            source = native/'src/gameplay/army/aircraft.cpp'
            sources.append({'path': 'src/gameplay/army/aircraft.cpp', 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
            extra = ['-DTHANDOR_NATIVE_AIRCRAFT_SOURCE='+json.dumps(str(source))]
        # Native global function-pointer slots require code below 2GB; overflow aborts.
        run(base+['-no-pie', '-I'+str(editor/'include')]+extra+
            [str(editor/'tests'/driver)]+linked+[str(library), '-Wl,--gc-sections', '-pthread', '-o', str(binary)],
            'link_'+label)
        binary.chmod(binary.stat().st_mode | 0o111)
        print(run([str(binary)]+([str(Path(sys.argv[4]).resolve())] if label == "aircraft" and len(sys.argv) == 5 else []), label+'_oracle').strip())
    (output/'native_oracle_results.json').write_text(json.dumps({
        'sources': sources, 'checks': records,
        'scope': 'Compiled native math, sampling, occupancy, RNG and aircraft arc/travel bodies. Host declarations and unused platform entry points are isolated; no full Windows game loop.'
    }, indent=2)+'\n')


if __name__ == '__main__':
    main()
