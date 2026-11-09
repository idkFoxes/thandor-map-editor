#!/usr/bin/env python3
"""Compare Lab terrain stamping to unmodified native code on the stock field.
Usage: script NATIVE_ELF_BUILD CORE_BUILD PCK_DIRECTORY
Build native_elf/build.py first; driver/bootstrap and original object files are reused.
"""
import json, subprocess, sys
from pathlib import Path
native,core,data=(Path(p).resolve() for p in sys.argv[1:])
editor=Path(__file__).resolve().parents[1]
manifest=json.loads((native/'source_manifest.json').read_text())
out=core/'native_stamp_v779';out.mkdir(exist_ok=True)
flags=manifest['flags']+['-I'+str(editor/'include'),'-DTHANDOR_NATIVE_DRIVER_INCLUDE='+json.dumps(str(native/'driver.cpp'))]
subprocess.run(flags+['-c',str(editor/'tests/native_terrain_stamp_v779_oracle.cpp'),'-o',str(out/'oracle.o')],check=True)
objects=[str(native/'objects'/(x['path'].removeprefix('src/').replace('/','_')+'.o')) for x in manifest['sources'] if x['path'].removeprefix('src/') not in manifest['excluded']]
exe=out/'oracle'
subprocess.run([flags[0],'-no-pie','-Wl,--gc-sections',*objects,str(native/'objects/posix_platform.o'),str(out/'oracle.o'),str(core/'libthandor_map_core.a'),'-pthread','-o',str(exe)],check=True)
subprocess.run([str(exe),'0',str(out/'initial'),'12345','mittelpunkt'],cwd=data,check=True)
