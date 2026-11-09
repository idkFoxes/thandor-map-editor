#!/usr/bin/env python3
"""Verify the original executable's research arithmetic, signed ordering and registry walk."""
import argparse,hashlib
from pathlib import Path
from verify_reset_binary import retail_va_to_file
from verify_retail_research_scoring import ANCHORS as EARLIER
ANCHORS={**EARLIER,
 0x53BAB4:('raw root MDL slot0 comparison','3b7920'),
 0x53BAF3:('raw root MDL slot7 comparison','3b793c'),
 0x53BB01:('child matching requires unlocked root first definition','ffb12000000050e883f2feff0f82bd000000'),
 0x53BB13:('first direct child and positive child count','8b810c00000083b90800000000'),
 0x53BB6E:('second direct child and count greater than one','8b811000000083b90800000001'),
 0x53BBB7:('research potential ARM scorer call','e8e4e6ffff'),
 0x53BBBC:('only strictly positive ARM scores accumulated','85c07e10'),
}

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail_executable',type=Path);a=ap.parse_args();data=a.retail_executable.read_bytes()
 expected='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969'
 actual=hashlib.sha256(data).hexdigest()
 if actual!=expected:raise SystemExit('FAIL original EXE identity: '+actual)
 print('EXE_SHA256',actual)
 for va,(name,raw) in ANCHORS.items():
  off=retail_va_to_file(data,va);expected_bytes=bytes.fromhex(raw)
  if data[off:off+len(expected_bytes)]!=expected_bytes:raise SystemExit(f'FAIL {va:08X} {name}')
  print(f'PASS {va:08X} {name}: {raw}')
 print('V637_RESEARCH_BINARY PASS; byte/branch audit, no original-game execution or address reconstruction')
if __name__=='__main__':main()
