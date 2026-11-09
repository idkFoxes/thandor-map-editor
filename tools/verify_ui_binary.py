#!/usr/bin/env python3
"""Original PE anchors for camera input, energy hierarchy, and native UI meter rounding."""
from pathlib import Path
import argparse,hashlib,json,struct
from verify_reset_binary import retail_va_to_file,EXPECTED_SHA256
ANCHORS={
 0x50C930:('camera uses native heading multiplier address','0faf0deeba5000'),
 0x50BAEE:('camera multiplier is 16, not placement 64','10000000'),
 0x52A6FC:('energy reads live runtime energy load +F4','8b96f4000000'),
 0x52A707:('active energy excludes switched-off bit1','f786ec00000001000000'),
 0x52A715:('definition flag80 gates direct child count','f7476880000000'),
 0x52A750:('energy walks direct attachment descriptors','83c620'),
 0x52D78C:('meter sixteenths numerator times32','c1e005'),
 0x52D795:('meter integer rounding and frame increment','f7f1c1e80140'),
 0x52D79D:('zero-demand meter frame17','b811000000'),
}
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('exe',type=Path);ap.add_argument('--output',type=Path);a=ap.parse_args();raw=a.exe.read_bytes()
 actual=hashlib.sha256(raw).hexdigest()
 if actual!=EXPECTED_SHA256:raise SystemExit('Original binary SHA mismatch')
 results=[]
 for va,(name,hexbytes) in ANCHORS.items():
  off=retail_va_to_file(raw,va);wanted=bytes.fromhex(hexbytes)
  if raw[off:off+len(wanted)]!=wanted:raise SystemExit(f'FAIL {va:08X} {name}')
  results.append({'va':f'0x{va:08X}','meaning':name,'bytes':hexbytes,'status':'PASS'})
  print('PASS',f'{va:08X}',name)
 if a.output:a.output.write_text(json.dumps({'sha256':actual,'anchors':results,'gzf_available':False},indent=2)+'\n')
