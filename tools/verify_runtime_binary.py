#!/usr/bin/env python3
"""Verify original ARM selection and live MDL replacement/health scaling instructions."""
import argparse
from pathlib import Path
from verify_reset_binary import retail_va_to_file
ANCHORS={
    0x51B3D4:('ARM eight linked slots','b908000000'),
    0x51B3DF:('ARM slot-zero fallback','8b9620000000'),
    0x51B3FC:('ARM required-technology check','e88ff90000'),
    0x51B403:('ARM last eligible selection','8bd0'),
    0x51B410:('ARM next slot','83c604'),
    0x51B414:('ARM scan continues after match','75da'),
    0x52ADA0:('candidate MDL requirement1C0','8b88c0010000'),
    0x52AEB5:('MDL six replacement slots','b906000000'),
    0x52AEC0:('MDL replacement list238','8b8738020000'),
    0x52AEC8:('MDL skips zero slots','7436'),
    0x52AECC:('MDL required-technology/registry check','e8bffeffff'),
    0x52AED1:('MDL skips locked/missing candidate','722d'),
    0x52AEDD:('live definition pointer swap','8906'),
    0x52AEDF:('runtime health3C','8b463c'),
    0x52AEE2:('signed health product new maximum60','f7af60000000'),
    0x52AEE8:('signed division previous maximum60','f7b960000000'),
    0x52AEEE:('store scaled health','89463c'),
    0x52AEF4:('rebuild owner selection metrics','e85714ffff'),
    0x52AEF9:('first eligible exits scan','eb0b'),
    0x52AF1C:('recursive child replacement','e87fffffff'),
}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path)
    data=p.parse_args().executable.read_bytes()
    for va,(name,raw) in ANCHORS.items():
        expected=bytes.fromhex(raw);off=retail_va_to_file(data,va)
        if data[off:off+len(expected)]!=expected:raise SystemExit(f'FAIL {va:08X} {name}')
        print(f'PASS {va:08X} {name}: {raw}')
