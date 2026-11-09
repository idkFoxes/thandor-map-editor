#!/usr/bin/env python3
"""Verify anchored reset call and instruction bytes in the original retail PE32.

This does not modify the executable. Does not claim GZF or GUI replay fidelity.
Usage: python3 tools/verify_reset_binary.py /path/to/thandor.exe
"""
import argparse
import hashlib
import struct
from pathlib import Path

EXPECTED_SHA256='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969'


def retail_va_to_file(data, va):
    if data[:2] != b'MZ':
        raise ValueError('not an MZ executable')
    pe_offset = struct.unpack_from('<I',data,0x3c)[0]
    if data[pe_offset:pe_offset+4] != b'PE\0\0':
        raise ValueError('not a PE executable')
    section_count, opt_size = struct.unpack_from('<HH',data,pe_offset+6)[0], struct.unpack_from('<H',data,pe_offset+20)[0]
    optional = pe_offset+24
    if struct.unpack_from('<H',data,optional)[0] != 0x10b:
        raise ValueError('not PE32')
    image_base=struct.unpack_from('<I',data,optional+28)[0]
    rva=va-image_base
    first_section=optional+opt_size
    for section_index in range(section_count):
        entry=first_section+section_index*40
        size, start, raw_size, raw_offset = struct.unpack_from('<IIII',data,entry+8)
        if start <= rva < start + min(size, raw_size):
            return raw_offset + rva - start
    raise ValueError(f'VA {va:08X} is not in a file-backed section')


def verify(data):
    cases = {
        0x0053B62F: ('test pioneer 0x100 entry flag',bytes.fromhex('F7461800010000')),
        0x0053B886: ('set pioneer 0x100',bytes.fromhex('814E1800010000')),
        0x0053B89D: ('call reset at 0x51C3E0',bytes.fromhex('E83E0BFEFF')),
        0x0051C3F3: ('reset movement flags mask preserving 0x100',bytes.fromhex('836318C6')),
        0x0051C3FF: ('conditional command mode low-bit test',bytes.fromhex('F7432C03000000')),
        0x0051C408: ('conditional command mode mask',bytes.fromhex('83632CFC')),
        0x0051C40C: ('conditional command mode OR 4',bytes.fromhex('834B2C04')),
        0x0051C426: ('reset fallback X',bytes.fromhex('8983B8000000')),
        0x0051C432: ('reset primary X',bytes.fromhex('894378')),
        0x0051C438: ('reset stored X',bytes.fromhex('894358')),
        0x0051C460: ('model hierarchy flags mask',bytes.fromhex('81A7EC000000E7FDFFFF')),
    }
    for va,(name,opcode) in cases.items():
        offset=retail_va_to_file(data,va)
        got=data[offset:offset+len(opcode)]
        if got!=opcode:
            raise ValueError(f'FAIL {name} at {va:08X}: got {got.hex()} expected {opcode.hex()}')
        print(f'PASS VA={va:08X} file_offset={offset:08X} {name}: {opcode.hex()}')
    call_offset=retail_va_to_file(data,0x0053B89D)
    disp=struct.unpack_from('<i',data,call_offset+1)[0]
    if 0x0053B89D+5+disp != 0x0051C3E0:
        raise ValueError('reset call target mismatch')
    print('PASS CALL_TARGET 0053B89D -> 0051C3E0')
    print('BOUNDARY: flags +0x18 is not runtime stateOrTechnologyId +0x100 or model class +0xEC')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('retail_executable',type=Path)
    args=parser.parse_args()
    data=args.retail_executable.read_bytes()
    actual_sha=hashlib.sha256(data).hexdigest()
    print('SHA256',actual_sha)
    if actual_sha!=EXPECTED_SHA256:
        parser.error('unknown executable SHA256; refusal to assert this binary contract')
    verify(data)
