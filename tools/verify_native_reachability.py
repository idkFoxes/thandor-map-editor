#!/usr/bin/env python3
"""Compile the supplied, unmodified native scratch-grid source and compare live fill results."""
from pathlib import Path
import subprocess,sys
native,build=map(lambda p:Path(p).resolve(),sys.argv[1:]);editor=Path(__file__).resolve().parents[1];out=build/'native_reachability_v775';out.mkdir(exist_ok=True)
flags=['g++','-std=c++20','-O1','-fwrapv','-fno-strict-aliasing','-mms-bitfields','-fshort-wchar','-ffunction-sections','-fdata-sections','-fno-pie','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(editor/'tools/native_oracle_stubs'),'-I'+str(native/'include'),'-I'+str(editor/'include')]
for s,o in [(native/'src/world/pathing/scratch_grid.cpp',out/'native.o'),(editor/'tests/movement_native_reachability_oracle.cpp',out/'test.o')]:subprocess.run(flags+['-c',str(s),'-o',str(o)],check=True)
subprocess.run(['g++','-no-pie','-Wl,--gc-sections',str(out/'native.o'),str(out/'test.o'),str(build/'libthandor_map_core.a'),'-pthread','-o',str(out/'oracle')],check=True)
subprocess.run([str(out/'oracle')],check=True)
