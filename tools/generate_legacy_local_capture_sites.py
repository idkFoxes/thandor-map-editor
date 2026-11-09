#!/usr/bin/env python3
"""Generate byte-checked local command handler/caller sites from native sources + the known EXE.
Usage: NATIVE_SOURCE_ROOT RETAIL_EXE OUTPUT_DIRECTORY
The generated manifest is used by the Windows overlay and the native-byte ABI verifier.
"""
from pathlib import Path
import re,subprocess,struct,json,sys,hashlib
if len(sys.argv)!=4:raise SystemExit(__doc__)
native,exe,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True);data=exe.read_bytes()
digest=hashlib.sha256(data).hexdigest()
if digest!='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969':raise ValueError('unknown original executable')
pe=struct.unpack_from('<I',data,0x3c)[0];n=struct.unpack_from('<H',data,pe+6)[0];opt=pe+24;section=opt+struct.unpack_from('<H',data,pe+20)[0];base=struct.unpack_from('<I',data,opt+28)[0]
sections=[struct.unpack_from('<IIII',data,section+i*40+8)for i in range(n)]
def at(va,n=16):
 rva=va-base
 for size,vaddr,rawsize,raw in sections:
  if vaddr<=rva<vaddr+max(size,rawsize):return data[raw+rva-vaddr:raw+rva-vaddr+n]
 raise ValueError(va)
functions=[]
for line in (native/'docs/original_addresses.txt').read_text().splitlines():
 m=re.match(r'0x([0-9A-Fa-f]+)\s+function\s+(\S+)\s+(\S+)',line)
 if m:functions.append((int(m[1],16),m[2],m[3]))
functions.sort();addresses={name:addr for addr,name,path in functions}
constants={k:int(v,0)for k,v in re.findall(r'inline constexpr auto (\w+) = (0x[0-9a-fA-F]+|\d+);',(native/'include/thandor/network/protocol/commands.h').read_text())}
s=(native/'src/network/protocol/commands.cpp').read_text().split('static const CommandTableEntry g_InGameCommandTable[] = {',1)[1].split('\n};',1)[0]
handlers=[]
for code,name in re.findall(r'\{(\w+), THANDOR_PTR\(&([\w]+)\)\}',s):
 code=constants[code] if code in constants else int(code,0)
 if code<=0xd0:continue
 addr=addresses[name];assert addr==0x55f130+code,(code,name,addr)
 # All candidate entry instructions are checked against objdump boundaries.
 ds=subprocess.check_output(['objdump','-D','-M','intel','--start-address='+hex(addr),'--stop-address='+hex(addr+20),str(exe)],text=True)
 instructions=[];size=0
 for line in ds.splitlines():
  m=re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)',line)
  if m:
   op=bytes.fromhex(m[2]);text=m[3];instructions.append((int(m[1],16),op.hex(),text));size+=len(op)
   if size>=5:break
 assert size>=5 and size<=16
 if any(not re.fullmatch(r'push\s+(eax|ebx|ecx|edx|esi|edi|ebp)|mov\s+ebp,esp',v[2])for v in instructions):raise ValueError('unsupported handler prologue: '+name)
 handlers.append({'name':name,'address':addr,'code':code,'size':size,'bytes':at(addr,size).hex(),'instructions':instructions})
issuers=[];calls=[];all_calls=[]
source_issuers={};dynamic_issuers={}
for path in {p for _,_,p in functions if p.startswith('src/ui/')}:
 source=native/path
 if not source.is_file():continue
 text=re.sub(r'/\*.*?\*/|//[^\n]*','',source.read_text(),flags=re.S)
 bodies={}
 for m in re.finditer(r'\b(\w+)\s*\([^{};]*\)\s*\{',text):
  if m[1] in {"if","while","for","switch","catch"} or text[:m.start()].count("{")!=text[:m.start()].count("}"):continue
  name=m[1];start=m.end();end=start;depth=1
  while depth and end<len(text):
   if text[end]=='{':depth+=1
   elif text[end]=='}':depth-=1
   end+=1
  bodies[name]=text[start:end]
 emitters={name for name,body in bodies.items() if 'InGameCommand_Issue' in body}
 changed=True
 while changed:
  old=set(emitters)
  for name,body in bodies.items():
   if any(re.search(r'\b'+re.escape(target)+r'\s*\(',body) for target in old):emitters.add(name)
  changed=old!=emitters
 source_issuers[path]=emitters
 dynamic={name for name,body in bodies.items() if 'InGameCommand_IssueHandler' in body}
 changed=True
 while changed:
  old=set(dynamic)
  for name,body in bodies.items():
   if any(re.search(r'\b'+re.escape(target)+r'\s*\(',body) for target in old):dynamic.add(name)
  changed=old!=dynamic
 dynamic_issuers[path]=dynamic
for i,(addr,name,path)in enumerate(functions[:-1]):
 if name not in source_issuers.get(path,set()):continue
 stop=functions[i+1][0]
 ds=subprocess.check_output(['objdump','-D','-M','intel','--start-address='+hex(addr),'--stop-address='+hex(stop),str(exe)],text=True)
 site=[]
 for line in ds.splitlines():
  m=re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)',line)
  if m and m[3].startswith('call'):
   op=bytes.fromhex(m[2]);pc=int(m[1],16);target=None
   if op[0]==0xe8 and len(op)==5:target=(pc+5+struct.unpack_from('<i',op,1)[0])&0xffffffff
   direct=next((h for h in handlers if h['address']==target),None)
   if direct or (target is None and name in dynamic_issuers.get(path,set())):
    site.append({'return_pc':pc+len(op),'opcode':op.hex(),'handler_code':direct['code']if direct else 0xffffffff,'function':name,'path':path})
 if site:issuers.append({'address':addr,'name':name,'path':path,'sites':len(site)});calls+=site
calls.sort(key=lambda s:s['return_pc'])
if len({c['return_pc']for c in calls})!=len(calls):raise ValueError('ambiguous native caller site')
for code in [0x3f0,0xd40,0xd70,0xfe0,0x12d0,0x13a0,0x14f0]:
 if not any(c['handler_code']==code for c in calls):raise ValueError('missing essential command caller '+hex(code))
result={'retail_exe_sha256':digest,'source_addresses_sha256':hashlib.sha256((native/'docs/original_addresses.txt').read_bytes()).hexdigest(),
 'handlers':handlers,'issuers':issuers,'sites':calls,
 'policy':'Source-resolved UI issuers, including same-file helper closure. Direct calls are code-specific; indirect calls require a source-resolved dynamic command issuer and approved UI return PC. Local role/player checks exclude AI and network execution.'}
(out/'recording_local_command_sites.json').write_text(json.dumps(result,indent=2)+'\n')
header='/* Generated from the original executable and source-resolved UI command issuers.\n   Original executable SHA256: '+digest+'\n   Regenerate with generate_legacy_local_capture_sites.py; do not guess new sites. */\n#ifndef THANDOR_RECORDING_LOCAL_COMMAND_SITES_H\n#define THANDOR_RECORDING_LOCAL_COMMAND_SITES_H\n#include "recording_local_command_hook.h"\nstatic const ThRecLocalHandler threc_local_handlers[] = {\n'
for h in handlers:
 header+='    {0x%08Xu,0x%Xu,%du,{%s}}, /* %s */\n'%(h['address'],h['code'],h['size'],','.join('0x'+h['bytes'][i:i+2]for i in range(0,len(h['bytes']),2)),h['name'])
header+='};\nstatic const ThRecLocalCaller threc_local_callers[] = {\n'
for c in calls:
 header+='    {0x%08Xu,0x%Xu,%du,{%s}}, /* %s */\n'%(c['return_pc'],c['handler_code'],len(bytes.fromhex(c['opcode'])),','.join('0x'+c['opcode'][i:i+2]for i in range(0,len(c['opcode']),2)),c['function'])
header+='};\n#define THREC_LOCAL_HANDLER_COUNT (sizeof(threc_local_handlers)/sizeof(threc_local_handlers[0]))\n#define THREC_LOCAL_CALLER_COUNT (sizeof(threc_local_callers)/sizeof(threc_local_callers[0]))\n#endif\n'
(out/'recording_local_command_sites.h').write_text(header)
print(json.dumps({'handlers':len(handlers),'issuers':len(issuers),'sites':len(calls),'indirect':sum(s['handler_code']==0xffffffff for s in calls)},indent=2))
