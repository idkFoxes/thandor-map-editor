#!/usr/bin/env python3
"""Original executable anchors supporting model memory, factory points and footprint detach."""
import argparse
from pathlib import Path
from verify_reset_binary import retail_va_to_file
ANCHORS={
    0x4BD211:('present models are visible','f7c104000000'),
    0x4BD223:('seen-before model branch','f7c108000000'),
    0x4BD235:('not-remembered flag10','f7c110000000'),
    0x4BD23D:('remembered RGB intensity135','b887000000'),
    0x4BD242:('remembered alpha255','bbff000000'),
    0x520875:('linked-parent MDL footprintDC','83b9dc00000000'),
    0x52087E:('child MDL footprintDC','83badc00000000'),
    0x52089A:('expanded parent footprint query','e8a1930000'),
    0x5208A1:('detach outside footprint','c786f000000000000000'),
    0x529C7C:('expanded radius loads MDLDC','8b87dc000000'),
    0x529C84:('expanded radius adds child radius','034520'),
    0x5248FE:('factory exit lookup key1/5','6a056a01'),
    0x524919:('factory spawn lookup key0/5','6a056a00'),
    0x524938:('heading toward transformed exit','e86302f6ff'),
}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path)
    data=p.parse_args().executable.read_bytes()
    for va,(name,raw) in ANCHORS.items():
        off=retail_va_to_file(data,va)
        if data[off:off+len(bytes.fromhex(raw))]!=bytes.fromhex(raw):
            raise SystemExit(f'FAIL {va:08X} {name}')
        print(f'PASS {va:08X} {name}: {raw}')
