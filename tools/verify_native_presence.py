#!/usr/bin/env python3
"""Check sparse faction-presence dilation against unmodified native scratch code."""
from pathlib import Path
import subprocess,sys
native,build=map(lambda p:Path(p).resolve(),sys.argv[1:]);editor=Path(__file__).resolve().parents[1];out=build/'native_presence_v776';out.mkdir(exist_ok=True)
flags=['g++','-std=c++20','-O1','-fwrapv','-fno-strict-aliasing','-mms-bitfields','-fshort-wchar','-ffunction-sections','-fdata-sections','-fno-pie','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(editor/'tools/native_oracle_stubs'),'-I'+str(native/'include'),'-I'+str(editor/'include')]
for p,o in [(native/'src/world/pathing/scratch_grid.cpp',out/'native.o'),(editor/'tests/movement_native_presence_v776_oracle.cpp',out/'test.o')]:subprocess.run(flags+['-c',str(p),'-o',str(o)],check=True)
exe=out/'oracle';subprocess.run(['g++','-no-pie','-Wl,--gc-sections',str(out/'native.o'),str(out/'test.o'),str(build/'libthandor_map_core.a'),'-pthread','-o',str(exe)],check=True);exe.chmod(0o755);subprocess.run([str(exe)],check=True)
