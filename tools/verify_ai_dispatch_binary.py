#!/usr/bin/env python3
"""Read-only opcode audit of the retail busy-unit cooldown and Pioneer order writes."""
import argparse
import hashlib
import struct
from pathlib import Path
from verify_reset_binary import EXPECTED_SHA256, retail_va_to_file

ANCHORS = {
    0x53B120: ('busy flags 0x13', 'f7401813000000'),
    0x53B127: ('idle dispatch', '7447'),
    0x53B129: ('locked flag 2', 'f7401802000000'),
    0x53B130: ('locked skip', '0f857a000000'),
    0x53B136: ('DEC cooldown +0x8C', 'ff888c000000'),
    0x53B13C: ('zero dispatch', '7432'),
    0x53B13E: ('nonnegative skip after DEC', '0f896c000000'),
    0x53B144: ('undo negative DEC', 'ff808c000000'),
    0x53B159: ('dispatch Pioneer', 'e8c2040000'),
    0x53B67A: ('bootstrap cooldown = 8', 'c7868c00000008000000'),
    0x52A7E5: ('dismantle skips regeneration', 'f786ec00000010000000'),
    0x52A7F5: ('subtract step from health delay', '2986f8000000'),
    0x52A7FB: ('nonnegative delay skip', '0f896f000000'),
    0x52A801: ('health delay resets to 4', 'c786f800000004000000'),
    0x52A82B: ('no regeneration flag', 'f786ec00000000040000'),
    0x52A837: ('health increment MDL +0x1B4', '0387b4010000'),
    0x52A850: ('health decrement MDL +0x1B4', '2b87b4010000'),
    0x4FED5E: ('water world-X projection coefficient', 'b89c6e1c00'),
    0x4FED69: ('water world-Y projection coefficient', 'b83437dfff'),
    0x4FEDCB: ('water top-left edge mask', 'f7465000600088'),
    0x4FEDDB: ('water diagonal edge mask', 'f7843ed000000000600088'),
    0x4FEDF2: ('water triangle selection', '7d3c'),
    0x4FEE24: ('water signed weighted Q12 result', '0facd00c'),
}

def verify(data):
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        raise ValueError('unknown retail executable SHA256')
    for va, (name, raw) in ANCHORS.items():
        offset = retail_va_to_file(data, va)
        if data[offset:offset + len(bytes.fromhex(raw))] != bytes.fromhex(raw):
            raise ValueError(f'opcode mismatch at {va:08X}: {name}')
        print(f'PASS {va:08X} {name}: {raw}')
    offset = retail_va_to_file(data, 0x53B159)
    if 0x53B159 + 5 + struct.unpack_from('<i', data, offset + 1)[0] != 0x53B620:
        raise ValueError('Pioneer dispatch call target mismatch')
    print('PASS Pioneer dispatch target 0053B620')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('retail_executable', type=Path)
    args = parser.parse_args()
    verify(args.retail_executable.read_bytes())
