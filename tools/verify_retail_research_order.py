#!/usr/bin/env python3
"""Check original executable anchors for research order, precedence and progress gates."""
import argparse
from pathlib import Path
from verify_reset_binary import retail_va_to_file
ANCHORS = {
    0x538159: ('workspace12 cap 32', '83f920'),
    0x538165: ('TEC11 exclusion', '83f80b'),
    0x5381DD: ('class13 precedes radar', '837f4c0d'),
    0x5381E3: ('class22 precedes radar', '837f4c16'),
    0x5381ED: ('radar183 follows class tests', '3db7000000'),
    0x538B3C: ('collect begins at slot28', 'b81c000000'),
    0x538B51: ('research table base excludes grant slot0', 'ffb483c4010000'),
    0x538B6C: ('descending slots until zero', '4875e1'),
    0x52A9CC: ('active research switched-off gate', 'f786ec00000001000000'),
    0x52AA2C: ('pending research running/producing gate', 'f786ec00000040010000'),
    0x53BA26: ('registry lookup tests MDL required technology', '3b88c0010000'),
}
if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('retail_executable',type=Path)
    data=parser.parse_args().retail_executable.read_bytes()
    for va,(name,raw) in ANCHORS.items():
        offset=retail_va_to_file(data,va)
        if data[offset:offset+len(bytes.fromhex(raw))]!=bytes.fromhex(raw):
            raise SystemExit(f'FAIL {va:08X} {name}')
        print(f'PASS {va:08X} {name}: {raw}')
