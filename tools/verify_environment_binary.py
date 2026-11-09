#!/usr/bin/env python3
"""Read-only AI placement anchors in the immutable original PE; GZF unavailable."""
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
anchors=[(5405870, '29 86 e8 00 00 00', 'subtract effect countdown'), (5405906, '83 bf 7c 01 00 00 00', 'random range zero gate'), (5405921, 'f7 b7 7c 01 00 00', 'random interval modulo'), (5405927, '03 97 78 01 00 00', 'add base interval'), (5406101, '6a 06', 'class6 attachment lookup'), (5406163, '8b 9f 74 01 00 00', 'land EFF reference'), (5406184, 'e8 b3 6d fd ff', 'nearest water sample'), (5406191, '7e 0a', 'positive-water selection gate'), (5406193, '8b 9f 58 00 00 00', 'water EFF alternate'), (5406272, '6a 00 6a 00', 'detached owner and no completion'), (5406276, 'e8 57 66 ff ff', 'create EFF runtime'), (5406281, 'ff 86 e0 00 00 00', 'advance persistent point index'), (5367113, 'f7 43 30 01 00 00 00', 'randomize orientation creation flag'), (5367128, 'ff 15 a0 30 40 00', 'creation orientation RNG'), (5367149, '89 97 14 00 00 00', 'randomized model angle2'), (5369096, 'ff 4e 10', 'decrement linked-shot frame counter'), (5369201, 'e8 3a d0 00 00', 'create linked SHT'), (5468546, '85 c9', 'test source damage counter'), (5468548, '0f 84 26 01 00 00', 'reject zero source counter')]

out=[]
for va,hexbytes,label in anchors:
 expected=bytes.fromhex(hexbytes);assert at(va,len(expected))==expected,(hex(va),label)
 out.append({'va':hex(va),'bytes':expected.hex(),'meaning':label,'status':'pass'})
root=Path(sys.argv[2]);sources={}
for rel in ['gameplay/army/weapons.cpp','world/effects/lifecycle.cpp','world/effects/pool.cpp','world/shots/flight.cpp','gameplay/ai/combat.cpp','gameplay/technology/runtime.cpp','world/model/pool.cpp']:
 q=root/'src'/rel;sources[rel]=hashlib.sha256(q.read_bytes()).hexdigest()
print(json.dumps({'exe_sha256':hashlib.sha256(b).hexdigest(),'anchors':out,'native_source_sha256':sources,'gzf':'unavailable'},indent=2))
