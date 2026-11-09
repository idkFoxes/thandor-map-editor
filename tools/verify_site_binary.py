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
anchors=[(0x53a222,'e8 89 ff ff ff','extractor candidate mode4 site gate'),
 (0x53a230,'e8 0b ea ff ff','assigned-entry count'),
 (0x53a235,'8d 48 03','assigned +3 divisor'),
 (0x53a23a,'6b 45 18 03','wrapping base weight times3'),
 (0x53a23e,'f7 f1','unsigned assigned-count division'),
 (0x53a240,'81 7d 1c 4a 01 00 00','ARM330 skips Tritium energy scaling'),
 (0x53a25e,'c1 e1 04','Tritium extraction rate times16'),
 (0x53a269,'f7 ea','signed full-width energy-demand multiply'),
 (0x53a26b,'f7 f9','signed extraction division'),
 (0x53a276,'e8 35 d2 ff ff','weighted candidate contribution after accepted site'),
 (0x539dbb,'e8 50 03 00 00','storage bonus calls additional-site reservation'),
 (0x53a137,'ff b7 44 01 00 00','KI81 clearance padding'),
 (0x53a13d,'6a 07','mode7 measured placement'),
 (0x53a15d,'6a 04','mode4 ordinary reach placement'),
 (0x53a167,'8b b7 70 00 00 00','KI28 separation quantum'),
 (0x53a173,'f7 f6','unsigned ceiling division'),
 (0x53a175,'83 f8 04','four-quantum upper bound'),
 (0x53a186,'e8 65 fd ff ff','call separated temporary-owner chain'),
 (0x53a00f,'3b 8f 70 00 00 00','Manhattan comparison to separation quantum'),
 (0x53a015,'0f 82 b5 00 00 00','strict unsigned below acceptance'),
 (0x53a021,'68 4d 01 00 00','temporary ARM333 id'),
 (0x53a044,'e8 a7 18 fe ff','temporary army creation'),
 (0x53a0d6,'e8 65 1f fe ff','reverse cleanup first retained temporary'),
 (0x53a0e6,'e8 55 1f fe ff','reverse cleanup next retained temporary'),
 (0x53a0f4,'e8 47 1f fe ff','reverse cleanup earliest retained temporary')]

out=[]
for va,hexbytes,label in anchors:
 expected=bytes.fromhex(hexbytes);assert at(va,len(expected))==expected,(hex(va),label)
 out.append({'va':hex(va),'bytes':expected.hex(),'meaning':label,'status':'pass'})
root=Path(sys.argv[2]);sources={}
for rel in ['gameplay/ai/placement.cpp','gameplay/ai/workspaces.cpp','gameplay/ai/purchasing.cpp',
 'gameplay/ai/construction.cpp','gameplay/army/placement.cpp','gameplay/army/collision.cpp','gameplay/army/pool.cpp','world/terrain/field_sampling.cpp']:
 q=root/'src'/rel;sources[rel]=hashlib.sha256(q.read_bytes()).hexdigest()
print(json.dumps({'exe_sha256':hashlib.sha256(b).hexdigest(),'anchors':out,'native_source_sha256':sources,'gzf':'unavailable'},indent=2))
