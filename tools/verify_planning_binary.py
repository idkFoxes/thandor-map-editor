#!/usr/bin/env python3
"""Check original executable identity and planning call order; no original-game execution."""
import argparse,hashlib,struct
from pathlib import Path
from verify_reset_binary import retail_va_to_file
from verify_retail_research_purchase_gates import ANCHORS
ANCHORS={**ANCHORS,0x53AC2E:('strategic ARM330 gate uses workspace ID lookup','684a010000e8b8dfffff'),0x53BF39:('research ARM330 gate uses workspace ID lookup','684a010000e8adccffff'),0x538C10:('workspace presence compares ID without runtime pointer test','3b4604750bf989ec')}
CALLS=[(0x53C92F,0x53AE60,'special site'),(0x53C936,0x53A6E0,'defense'),(0x53C93D,0x53A800,'exploration'),(0x53C944,0x53A8D0,'attack'),(0x53C94B,0x53AC20,'strategic factories'),(0x53C952,0x53B070,'strategic labs'),(0x53C959,0x53BF30,'research'),(0x53C95F,0x5378C0,'affordable dispatch')]
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail_executable',type=Path);a=ap.parse_args();data=a.retail_executable.read_bytes();h=hashlib.sha256(data).hexdigest();assert h=='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969';print('EXE_SHA256',h)
 for va,(label,raw) in ANCHORS.items():
  off=retail_va_to_file(data,va);assert data[off:off+len(bytes.fromhex(raw))]==bytes.fromhex(raw),(hex(va),label);print(f'PASS {va:08X} {label}')
 for va,target,label in CALLS:
  off=retail_va_to_file(data,va);assert data[off]==0xE8 and va+5+struct.unpack_from('<i',data,off+1)[0]==target,(hex(va),label);print(f'PASS {va:08X} call {target:08X} {label}')
 assert all(a[0]<b[0] for a,b in zip(CALLS,CALLS[1:]));print('V639_PLANNING_BINARY PASS anchors=48; native call order, no original-game execution')
if __name__=='__main__':main()
