#!/usr/bin/env python3
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, os, subprocess, sys
p=argparse.ArgumentParser(description='Build original Open Thandor simulation as a Linux x86-64 ELF.')
p.add_argument('--source',type=Path,default=Path(__file__).resolve().parent.parent/'reference/open-thandor-main');p.add_argument('--jobs',type=int,default=2)
p.add_argument('--output',type=Path,help='Build and log directory')
p.add_argument('--jandrenator',action='store_true',help='Build the Test15 Simulation Lab adapter')
p.add_argument('--windows',action='store_true',help='Target Windows x86-64 with MinGW-w64')
a=p.parse_args();r=Path(__file__).resolve().parent;n=a.source.resolve()
out=a.output.resolve() if a.output else r
out.mkdir(parents=True,exist_ok=True)
if not (n/'include/thandor/thandor.h').is_file():p.error('supply --source /path/to/open-thandor-main')
if not a.windows and (not sys.platform.startswith('linux') or os.uname().machine!='x86_64'):p.error('requires Linux x86-64 MAP_32BIT')
for d in ['objects','logs']:(out/d).mkdir(exist_ok=True)
(out/'native_simulation_tick.cpp').write_text('#include '+json.dumps(str(n/'src/gameplay/session/tick.cpp'))+'\n')
flags=[os.getenv('CXX','x86_64-w64-mingw32-g++' if a.windows else 'g++'),'-std=c++20','-O1','-g','-fwrapv','-fno-strict-aliasing','-ffp-contract=off','-mms-bitfields','-fshort-wchar','-ffunction-sections','-fdata-sections','-fno-pie','-include','strings.h','-D_strnicmp=strncasecmp','-D__forceinline=inline','-D__cdecl=','-D__stdcall=','-D__fastcall=','-D__declspec(x)=','-I'+str(out),'-I'+str(r/'compat'),'-I'+str(n/'include')]
if a.windows:
 flags.remove('-fno-pie')
 flags.remove('-fshort-wchar')
 flags[flags.index('-include')+1]='string.h'
 flags.remove('-D_strnicmp=strncasecmp')
 flags+=['-DTHANDOR_WINDOWS_HEADLESS','-flto']
if a.jandrenator:flags+=['-DTHANDOR_JANDRENATOR_HEADLESS']
paths=[]
for d in ['assets','world','gameplay','core/math','core/text','graphics/resources','graphics/render','graphics/core','graphics/terrain','graphics/backend','audio']:paths+=list((n/'src'/d).rglob('*.cpp'))
paths += [n/'src'/v for v in ['core/memory/allocator.cpp','core/memory/synchronization.cpp','core/settings/persistent.cpp','platform/bootstrap/runtime.cpp','ui/core/model_tint.cpp','ui/frontend/player.cpp','ui/frontend/main_loop.cpp','ui/ingame/camera_commands.cpp','ui/ingame/key_commands.cpp','ui/ingame/layout.cpp','platform/system/time_locale.cpp']]
excluded={'gameplay/session/tick.cpp':'Included unchanged in driver.cpp to call private simulation step.','gameplay/session/loading_movie.cpp':'No movie/UI/network pacing; freeze during offline bootstrap.','gameplay/army/preview.cpp':'Omit thumbnail preview armies; shared aircraft IDs retained verbatim.'}
paths=sorted(set(paths));sources=[s for s in paths if s.relative_to(n/'src').as_posix() not in excluded]
h=hashlib.sha256(json.dumps(flags).encode())
for f in sorted(list((n/'include').rglob('*.h'))+list((r/'compat').rglob('*'))):
 if f.is_file():h.update(f.read_bytes())
base=h.digest()
def obj(s):return out/'objects'/((s.relative_to(n/'src').as_posix().replace('/','_') if s.is_relative_to(n/'src') else s.stem)+'.o')
def build(s):
 target=obj(s);stamp=target.with_suffix('.fingerprint');h=hashlib.sha256(base+s.read_bytes())
 if s.name=='driver.cpp':h.update((r/'trace.hpp').read_bytes());h.update((r/'jandrenator_trace.hpp').read_bytes());h.update((n/'src/gameplay/session/tick.cpp').read_bytes())
 fp=h.hexdigest()
 if target.exists() and stamp.exists() and stamp.read_text()==fp:return 0
 q=subprocess.run(flags+['-c',str(s),'-o',str(target)],capture_output=True,text=True);(out/'logs'/(target.stem+'.log')).write_text(q.stdout+q.stderr)
 if q.returncode:print(q.stderr,flush=True)
 else:stamp.write_text(fp)
 return q.returncode
failed=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(a.jobs,4))) as pool:
 for i,code in enumerate(pool.map(build,sources+[r/'driver.cpp',r/'posix_platform.cpp']),1):
  if code:failed.append(i)
  if i%20==0:print(f'compile {i}/{len(sources)+2} failures={len(failed)}',flush=True)
(out/'source_manifest.json').write_text(json.dumps({'native_source':str(n),'flags':flags,'excluded':excluded,'sources':[{'path':str(s.relative_to(n)),'sha256':hashlib.sha256(s.read_bytes()).hexdigest()} for s in paths],'simulation_modified':False},indent=2)+'\n')
if failed:sys.exit(1)
link_flags=['-static','-flto','-fwhole-program','-Wl,--gc-sections,--image-base,0x400000,--disable-dynamicbase,--disable-high-entropy-va'] if a.windows else ['-no-pie','-Wl,--gc-sections']
q=subprocess.run([flags[0],*link_flags,*[str(obj(s)) for s in sources+[r/'driver.cpp',r/'posix_platform.cpp']],'-o',str(out/'open-thandor-headless.part')],capture_output=True,text=True);(out/'logs/link.log').write_text(q.stdout+q.stderr)
if q.returncode:print(q.stderr);sys.exit(q.returncode)
name=('jandrenator-headless' if a.jandrenator else 'open-thandor-headless')+('.exe' if a.windows else '')
(out/'open-thandor-headless.part').replace(out/name);print(f'Built {name}',flush=True)
