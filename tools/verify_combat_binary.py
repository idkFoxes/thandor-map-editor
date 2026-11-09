#!/usr/bin/env python3
"""Verify command lifecycle bytes in the supplied retail executable; never execute it."""
import argparse,hashlib,struct
from pathlib import Path
from verify_reset_binary import retail_va_to_file
from verify_planning_binary import ANCHORS,CALLS
COMMAND_ANCHORS={
 0x536FCF:('interrupted command guard','f7462c04000000'),
 0x536FDC:('signed generation guard','83be3000000000'),
 0x536FE5:('army or position command guard','f7462c03000000'),
 0x537001:('retained generation load','8b1d90b35100'),
 0x537018:('stored assigned target fallback','ffb698000000'),
 0x53702C:('fallback interruption','834e2c04'),
 0x537032:('candidate generation load','8b1d8cb35100'),
 0x537045:('candidate target shift','d3eb'),
 0x537047:('AI combat command bit','834e2c08'),
 0x51D2CB:('AI callback after hierarchy','e8f09c0100'),
 0x51D2D5:('generation decrements by simulation step','294730'),
 0x51D2DA:('negative generation clears interruption and AI bits','83672cf3'),
 0x51C54C:('standard generation load','8b0d88b35100'),
 0x51C576:('resolve target sets army bit','c7432c01000000'),
 0x51C582:('resolve null clears command','c7432c00000000'),
 0x51B388:('standard generation1024','00040000'),
 0x51B38C:('candidate generation32','20000000'),
 0x51B390:('retained generation512','00020000'),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_executable',type=Path);a=p.parse_args();data=a.retail_executable.read_bytes();h=hashlib.sha256(data).hexdigest();assert h=='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969';print('EXE_SHA256',h)
 for va,(label,raw) in {**ANCHORS,**COMMAND_ANCHORS}.items():
  off=retail_va_to_file(data,va);expected=bytes.fromhex(raw);assert data[off:off+len(expected)]==expected,(hex(va),label);print(f'PASS {va:08X} {label}')
 for va,target,label in CALLS:
  off=retail_va_to_file(data,va);assert data[off]==0xE8 and va+5+struct.unpack_from('<i',data,off+1)[0]==target,(hex(va),label);print(f'PASS {va:08X} call {target:08X} {label}')
 print(f'V640_COMBAT_BINARY PASS anchors={len({**ANCHORS,**COMMAND_ANCHORS})+len(CALLS)}; no original-game execution')
if __name__=='__main__':main()
