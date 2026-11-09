#!/usr/bin/env python3
"""Verify the original executable's research arithmetic, signed ordering and registry walk."""
import argparse,hashlib
from pathlib import Path
from verify_reset_binary import retail_va_to_file
from verify_retail_research_order import ANCHORS as EARLIER
ANCHORS={**EARLIER,
 0x50D848:('new owner becomes world list head, next points to previous head','8783d8000000c7870000000000000000898704000000'),
 0x53BA09:('registry starts at first of 768 slots','bbf0ee5100ba00030000'),
 0x53BA20:('registry first matching technology returns without sorting','8b0385c0740a3b88c0010000740a'),
 0x528972:('MDL records start at 0x200, declared record count','8b8eb000000081c600020000'),
 0x52899B:('MDL registration advances by serialized byte size','03b6000000004975ec'),
 0x53BDC7:('unsupported class preserves lookup EAX through common return','0f85ac000000'),
 0x53BE79:('common return leaves EAX unchanged','89ec5d5e5f5a595bc20c00'),
 0x53BDE5:('kind3 dword multiplication wraps BEFORE shift','69c0409c0000c1e8080faf8638020000c1e808'),
 0x53BD23:('pump pressure comparison is signed','3df00000007c26'),
 0x53BD2A:('pump dword multiplication wraps BEFORE shift','0faf8638020000c1e808'),
 0x53BFC2:('best score comparison signed strict greater','3bc37e0a'),
 0x53BC00:('compatibility callback accepts and preserves EAX','505351525589e5f889ec5d5a595b58c20800'),
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
 print('V636_RESEARCH_BINARY PASS; byte/branch audit, no original-game execution or address reconstruction')
if __name__=='__main__':main()
