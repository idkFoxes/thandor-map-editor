#!/usr/bin/env python3
"""Verify supplied original EXE command/follow gates; never execute it."""
import argparse,hashlib,struct
from pathlib import Path
from verify_reset_binary import retail_va_to_file
from verify_follow_binary import ANCHORS,COMMAND_ANCHORS,CALLS,FOLLOW_ANCHORS
from verify_stride_binary import STRIDE_ANCHORS
AUDIO_ANCHORS={
 0x50BD72:('world-view sound height uses signed distance/4','c1fa02'),
 0x50BD8A:('world-view rebuilds target-based listener','e871f8ffff'),
 0x52442B:('structure factory switched-off gate','f786ec00000001000000'),
 0x52443B:('structure factory research gate','f786ec00000040000000'),
 0x524447:('structure factory building state1','83beb800000001'),
 0x524450:('structure factory primary sound slot','8b91ac010000'),
 0x524DBB:('unit factory switched-off gate','f786ec00000001000000'),
 0x524DCB:('unit factory research gate','f786ec00000040000000'),
 0x524DD7:('unit factory building state1','83beb800000001'),
 0x524E36:('door channel excludes building','83beb800000001'),
 0x524E43:('independent door sound slot274','8b9174020000'),
 0x524E49:('door channel excludes idle','83beb800000000'),
 0x52C1D1:('live shot sound slot280','8b9080020000'),
 0x52C204:('live shot gain288','ffb388020000'),
 0x52C20A:('live shot maximum distance28C','ffb38c020000'),
 0x51E74A:('effect creation visibility test','e8318efeff'),
 0x51E755:('effect sound gain6C','ffb36c000000'),
 0x51E761:('effect creation positioned one-shot','e87acffeff'),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_executable',type=Path);a=p.parse_args();data=a.retail_executable.read_bytes();h=hashlib.sha256(data).hexdigest();assert h=='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969';print('EXE_SHA256',h)
 anchors={**ANCHORS,**COMMAND_ANCHORS,**FOLLOW_ANCHORS,**STRIDE_ANCHORS,**AUDIO_ANCHORS}
 for va,(label,raw) in anchors.items():
  off=retail_va_to_file(data,va);expected=bytes.fromhex(raw);assert data[off:off+len(expected)]==expected,(hex(va),label);print(f'PASS {va:08X} {label}')
 for va,target,label in CALLS:
  off=retail_va_to_file(data,va);assert data[off]==0xE8 and va+5+struct.unpack_from('<i',data,off+1)[0]==target,(hex(va),label);print(f'PASS {va:08X} call {target:08X} {label}')
 print(f'V643_AUDIO_BINARY PASS anchors={len(anchors)+len(CALLS)}; no original-game execution')
if __name__=='__main__':main()
