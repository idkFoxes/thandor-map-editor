#!/usr/bin/env python3
"""Verify the original executable's research arithmetic, signed ordering and registry walk."""
import argparse,hashlib
from pathlib import Path
from verify_reset_binary import retail_va_to_file
from verify_retail_research_model_matching import ANCHORS as EARLIER
ANCHORS={**EARLIER,
 0x537916:('local planning budget subtraction and immediate unaffordable return','2bd87236'),
 0x53791C:('producer check follows local budget subtraction','e80ffdffff'),
 0x537925:('eligible candidate dispatch after budget and producer check','e8d6feffff'),
 0x537674:('producer class reuse tests complete raw flag mask','851d18745300'),
 0x5376B2:('factory purchase runtime flags 0xC9 gate','f787ec000000c9000000'),
 0x5376C6:('factory purchase idle behavior gate','83bfb800000000'),
 0x53743F:('at most three sorted candidates saved for replay','83f9037c05b903000000'),
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
 print('V638_RESEARCH_BINARY PASS; byte/branch audit, no original-game execution or address reconstruction')
if __name__=='__main__':main()
