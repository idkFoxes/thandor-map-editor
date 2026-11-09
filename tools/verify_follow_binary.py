#!/usr/bin/env python3
"""Verify supplied original EXE command/follow gates; never execute it."""
import argparse,hashlib,struct
from pathlib import Path
from verify_reset_binary import retail_va_to_file
from verify_combat_binary import ANCHORS,COMMAND_ANCHORS,CALLS
FOLLOW_ANCHORS={5387103: ('weapon line-of-fire callback', 'e86c860000'), 5387116: ('first-attachment primary weapon', '3bb040010000'), 5387133: ('movement target-follow flag0x20', 'f7421820000000'), 5387149: ('command mode0x8', 'f7422c08000000'), 5387158: ('ordinary fallback start', 'e8f597ffff'), 5387165: ('AI clamped start', 'e8be98ffff'), 5387200: ('reset only active target-follow state', 'f7421820000000'), 5387219: ('ordinary queued-target-preserving reset', 'e8589affff'), 5387226: ('AI current-position reset', 'e8b19affff'), 5360551: ('routed/locked guard0x202', 'f7471802020000'), 5360564: ('retry countdown guard', '83bfa400000000'), 5360601: ('fallback source', '8db7b8000000'), 5360607: ('waypoint destination eight bytes later', '81c7c0000000'), 5360613: ('16-dword forward overlap copies fallback to eight points', 'b910000000'), 5360632: ('queue count compares with8', '398fa8000000'), 5360699: ('retry count0x40', 'c787a400000040000000'), 5360759: ('clamped active/locked guard', 'f7471803000000'), 5360772: ('clamped retry guard', '83bfa400000000'), 5360798: ('two-unit hop threshold', '3d00200000'), 5360844: ('clamped flags clear', '816718a7fbffff'), 5360851: ('clamped flags set', '814f18a1000000'), 5360901: ('clamped retry64', 'c787a400000040000000'), 5361213: ('ordinary reset queue guard', 'f7431808000000'), 5361271: ('ordinary reset clear flags', '8163180fffffff'), 5361309: ('AI reset queue guard', 'f7431808000000'), 5361354: ('AI reset clear flags', '8163180fffffff'), 5485577: ('AI workspace point routes only', 'e8d214feff'), 5485669: ('AI anchor point routes only', 'e87614feff'), 5387688: ('follow requires pitch alignment', '3bc2'), 5387702: ('follow shared reload gate', '83be2400000000'), 5387728: ('ready weapon invokes follow', 'e86bfdffff'), 5362429: ('movement retry decremented once per callback', 'ff8fa4000000')}
FOLLOW_ANCHORS.update({0x51C729:('command aim selects army flag','f7432c01000000'),0x51C78E:('command aim tests current faction visibility','854650'),0x51C750:('invisible command target cleared','c7431c00000000'),0x51C757:('invisible command flags cleared','c7432c00000000')})
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_executable',type=Path);a=p.parse_args();data=a.retail_executable.read_bytes();h=hashlib.sha256(data).hexdigest();assert h=='53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969';print('EXE_SHA256',h)
 anchors={**ANCHORS,**COMMAND_ANCHORS,**FOLLOW_ANCHORS}
 for va,(label,raw) in anchors.items():
  off=retail_va_to_file(data,va);expected=bytes.fromhex(raw);assert data[off:off+len(expected)]==expected,(hex(va),label);print(f'PASS {va:08X} {label}')
 for va,target,label in CALLS:
  off=retail_va_to_file(data,va);assert data[off]==0xE8 and va+5+struct.unpack_from('<i',data,off+1)[0]==target,(hex(va),label);print(f'PASS {va:08X} call {target:08X} {label}')
 print(f'V641_FOLLOW_BINARY PASS anchors={len(anchors)+len(CALLS)}; no original-game execution')
if __name__=='__main__':main()
