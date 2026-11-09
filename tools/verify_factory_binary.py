#!/usr/bin/env python3
"""Original executable anchors for rotated factory exit placement and retail target markers."""
import argparse
from pathlib import Path
from verify_reset_binary import retail_va_to_file
ANCHORS={
 0x5244CE:('common building placement before exit check','e8fd330000'),
 0x5244F3:('SPR anchor lookup','e8f8a1f9ff'),
 0x524504:('anchor angle/length','e86706f6ff'),
 0x524514:('rotated anchor vector','e8e705f6ff'),
 0x524526:('anchor runtime clearance 0xC00','68000c0000'),
 0x52452E:('anchor checks runtime list','e83d580000'),
 0x524547:('anchor terrain radius 0xC00','68000c0000'),
 0x52454C:('contact-specific terrain callback','ff1495f0fa5100'),
 0x52467E:('existing factory anchor exclusion radius','81c2000c0000'),
 0x568289:('marker uses top surface height','e8626df9ff'),
 0x56829C:('native marker creates EFF','e8ff61fbff'),
 0x5682CC:('marker unity scale Q12','81fa00100000'),
}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path)
 data=p.parse_args().executable.read_bytes()
 for address,(name,hex_bytes) in ANCHORS.items():
  raw=bytes.fromhex(hex_bytes);offset=retail_va_to_file(data,address)
  if data[offset:offset+len(raw)]!=raw:raise SystemExit(f'FAIL {address:08X} {name}')
  print(f'PASS {address:08X} {name}')
