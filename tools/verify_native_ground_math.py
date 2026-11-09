#!/usr/bin/env python3
"""Compare ground steering and quantized positions with unmodified native math/steering."""
from pathlib import Path
import subprocess,sys
native,build=map(lambda x:Path(x).resolve(),sys.argv[1:]);editor=Path(__file__).resolve().parents[1];out=build/'native_ground_math_v776';out.mkdir(exist_ok=True)
flags=['g++','-std=c++20','-O1','-fwrapv','-fno-strict-aliasing','-mms-bitfields','-fshort-wchar','-ffunction-sections','-fdata-sections','-fno-pie','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(editor/'tools/native_oracle_stubs'),'-I'+str(native/'include'),'-I'+str(editor/'include')]
source=out/'test.cpp';source.write_text('#include '+__import__('json').dumps(str(native/'src/gameplay/army/drive_ground.cpp'))+'\n#include '+__import__('json').dumps(str(editor/'tests/movement_native_ground_math_v776_oracle.cpp'))+'\n')
for p,o in [(source,out/'test.o'),(native/'src/core/math/fixed_trig.cpp',out/'math.o'),(native/'src/core/math/fixed_vector.cpp',out/'vector.o')]:subprocess.run(flags+['-c',str(p),'-o',str(o)],check=True)
exe=out/'oracle';subprocess.run(['g++','-no-pie','-Wl,--gc-sections',str(out/'test.o'),str(out/'math.o'),str(out/'vector.o'),str(build/'libthandor_map_core.a'),'-pthread','-o',str(exe)],check=True)
exe.chmod(0o755);subprocess.run([str(exe)],check=True)
