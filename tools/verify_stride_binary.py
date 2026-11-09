#!/usr/bin/env python3
"""Verify supplied original EXE command/follow gates; never execute it."""
import argparse,hashlib,struct
from pathlib import Path
from verify_reset_binary import retail_va_to_file
from verify_follow_binary import ANCHORS,COMMAND_ANCHORS,CALLS,FOLLOW_ANCHORS
STRIDE_ANCHORS={
 0x5220F7:('left reach adds MDL stride C0','0383c0000000'),
 0x5220FD:('left reach compares with route length','3b4524'),
 0x522122:('left clipped reach consumes current route point','e8b9adffff'),
 0x522138:('left frozen endpoint X','898690000000'),
 0x5222B3:('left rate denominator travel+2*C4','8d0c48'),
 0x5222C6:('left rate C0 shifted13','c1e00d'),
 0x522357:('right reach adds MDL stride C0','0383c0000000'),
 0x52235D:('right reach compares with route length','3b4524'),
 0x522382:('right clipped reach consumes current route point','e859abffff'),
 0x522398:('right frozen endpoint X','898694000000'),
 0x522513:('right rate denominator travel+2*C4','8d0c48'),
 0x522526:('right rate C0 shifted13','c1e00d'),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_executable',type=Path);a=p.parse_args();data=a.retail_executable.read_bytes();h=hashlib.sha256(data).hexdigest();assert h=='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969';print('EXE_SHA256',h)
 anchors={**ANCHORS,**COMMAND_ANCHORS,**FOLLOW_ANCHORS,**STRIDE_ANCHORS}
 for va,(label,raw) in anchors.items():
  off=retail_va_to_file(data,va);expected=bytes.fromhex(raw);assert data[off:off+len(expected)]==expected,(hex(va),label);print(f'PASS {va:08X} {label}')
 for va,target,label in CALLS:
  off=retail_va_to_file(data,va);assert data[off]==0xE8 and va+5+struct.unpack_from('<i',data,off+1)[0]==target,(hex(va),label);print(f'PASS {va:08X} call {target:08X} {label}')
 print(f'V642_STRIDE_BINARY PASS anchors={len(anchors)+len(CALLS)}; no original-game execution')
if __name__=='__main__':main()
