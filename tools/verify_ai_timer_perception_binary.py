#!/usr/bin/env python3
"""Verify original PE bytes for alert decay, visibility and friendly threshold."""
import argparse,hashlib,json
from pathlib import Path
from verify_reset_binary import retail_va_to_file,EXPECTED_SHA256
ANCHORS={
 0x5131FA:('primary alert nonzero guard','3946587603'),
 0x5131FF:('primary alert decrement','ff4e58'),
 0x513202:('secondary alert nonzero guard','3946647603'),
 0x513207:('secondary alert decrement','ff4e64'),
 0x513210:('faction record stride','81c640070000'),
 0x538366:('hostile capability query','e835b9fdff'),
 0x538371:('current visibility test','f7c302000000'),
 0x538379:('discovery visibility test','f7c301000000'),
 0x538393:('discovered Group-A handler gate','813c9d98ff5100d0745200'),
 0x513F9D:('friendly threshold four','ba04000000'),
 0x513FA9:('first friendly capability comparison','3bda7208'),
 0x513FC7:('second friendly capability comparison','3bda7208'),
}
def verify(data):
 sha=hashlib.sha256(data).hexdigest()
 if sha!=EXPECTED_SHA256:raise ValueError('unexpected original executable SHA256')
 result={'retail_executable_sha256':sha,'anchors':[],'gzf':'not available','gui':'not run'}
 for va,(name,raw) in ANCHORS.items():
  offset=retail_va_to_file(data,va);expected=bytes.fromhex(raw)
  if data[offset:offset+len(expected)]!=expected:raise ValueError(f'FAIL {va:08X}: {name}')
  result['anchors'].append({'va':f'0x{va:08X}','name':name,'bytes':raw,'status':'pass'})
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_executable',type=Path)
 p.add_argument('--json',type=Path);args=p.parse_args();result=verify(args.retail_executable.read_bytes())
 if args.json:args.json.write_text(json.dumps(result,indent=2)+'\n')
 for anchor in result['anchors']:print('PASS',anchor['va'],anchor['name'])
