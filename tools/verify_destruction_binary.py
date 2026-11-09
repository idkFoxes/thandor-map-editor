#!/usr/bin/env python3
"""Read-only checks against the immutable original PE; no GZF correspondence claim."""
import struct,hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]);b=p.read_bytes()
assert hashlib.sha256(b).hexdigest()=='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969'
pe=struct.unpack_from('<I',b,0x3c)[0];count=struct.unpack_from('<H',b,pe+6)[0]
opt=pe+24;image=struct.unpack_from('<I',b,opt+28)[0];secs=opt+struct.unpack_from('<H',b,pe+20)[0]
def at(va,n):
 rva=va-image
 for i in range(count):
  off=secs+i*40;size,virtual,rawsize,raw=struct.unpack_from('<IIII',b,off+8)
  if virtual<=rva<virtual+rawsize:return b[raw+rva-virtual:raw+rva-virtual+n]
 raise ValueError(hex(va))
anchors=[(0x51ee4c,'e8 0f a7 00 00','completion invokes hierarchy detach/destroy'),
 (0x51edc0,'ff 4e 20','kind-1 completion countdown DEC'),
 (0x51e6aa,'8b 8b 24 00 00 00','copy EFF +0x24 completion counter'),
 (0x51e6ca,'89 4e 20','store runtime completion counter at +0x20'),
 (0x52a25f,'89 76 38','direct damage assigns terminal self link'),
 (0x52a345,'89 76 38','impact damage assigns terminal self link'),
 (0x52a960,'f7 86 ec 00 00 00 20 00 00 00','destruction-started guard'),
 (0x52a96c,'83 7e 3c 00','terminal health guard'),
 (0x52a972,'83 7e 38 00','terminal link guard, no parent requirement'),
 (0x52a982,'e8 f9 ef ff ff','service root or linked destruction channels'),
 (0x52a987,'ff 4e 40 ff 4e 44 ff 4e 48 ff 4e 4c ff 4e 50 ff 4e 54 ff 4e 58 ff 4e 5c','eight compare-before-decrement channels')]
out=[]
for va,hexbytes,label in anchors:
 expected=bytes.fromhex(hexbytes);assert at(va,len(expected))==expected,(hex(va),label)
 out.append({'va':hex(va),'bytes':expected.hex(),'meaning':label,'status':'pass'})
root=Path(sys.argv[2]);sources={}
for rel in ['gameplay/army/damage.cpp','gameplay/army/class_dispatch.cpp','gameplay/army/weapons.cpp',
 'gameplay/army/factory.cpp','gameplay/army/placement_release.cpp','world/effects/lifecycle.cpp','world/effects/pool.cpp','world/model/pool.cpp']:
 q=root/'src'/rel;sources[rel]=hashlib.sha256(q.read_bytes()).hexdigest()
print(json.dumps({'exe_sha256':hashlib.sha256(b).hexdigest(),'anchors':out,'native_source_sha256':sources,'gzf':'unavailable'},indent=2))
