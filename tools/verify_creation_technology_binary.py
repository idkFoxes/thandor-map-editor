#!/usr/bin/env python3
"""Read-only retail creation-grant and planner gate opcode anchors."""
import argparse
import hashlib
from pathlib import Path
from verify_reset_binary import EXPECTED_SHA256, retail_va_to_file
ANCHORS = {
  0x51B9D9: ('creation flags test bit 4', 'f7451c04000000'),
  0x51B9E6: ('creation calls hierarchy grant', 'e815210000'),
  0x51DB24: ('grant selects linked MDL', 'e807d9ffff'),
  0x51DB2D: ('grant calls MDL technology forwarder', 'e8aed20000'),
  0x52ADF1: ('granted technology MDL +1C4', 'ffb0c4010000'),
  0x52ADFB: ('forwarder calls unlock', 'e8c08bfeff'),
  0x51DA90: ('availability checks linked slot zero', 'ffb620000000'),
  0x539EBC: ('power numerator KI index zero', '8b8f00000000'),
  0x539ECC: ('power denominator KI index 13', 'f7bf4c000000'),
  0x53AC4D: ('advanced factory capacity KI index 11', 'ffb744000000'),
  0x53AC9F: ('basic factory capacity KI index 10', 'ffb740000000'),
}
def verify(data):
    if hashlib.sha256(data).hexdigest()!=EXPECTED_SHA256:
        raise ValueError('unknown retail executable SHA256')
    for va,(name,raw) in ANCHORS.items():
        off=retail_va_to_file(data,va)
        if data[off:off+len(bytes.fromhex(raw))]!=bytes.fromhex(raw):
            raise ValueError(f'opcode mismatch {va:08X}: {name}')
        print(f'PASS {va:08X} {name}: {raw}')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('retail_executable',type=Path)
    verify(p.parse_args().retail_executable.read_bytes())
