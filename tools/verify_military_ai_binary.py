#!/usr/bin/env python3
"""Original retail military score, group and impact-alert opcode anchors."""
import argparse
from pathlib import Path
from verify_creation_technology_binary import verify as verify_creation
from verify_reset_binary import retail_va_to_file
ANCHORS={
  0x5252A0:('class15 selects placement child1','8b83d0000000'),
  0x5252B9:('class15 clears placement child1','c783d000000000000000'),
  0x52479E:('class13 tests placement child3','83bfd800000000'),
  0x5247B2:('class13 clears placement child3','c787d800000000000000'),
  0x52CC82:('repair classification uses SHT damage slot0','83b8b000000000'),
  0x52CC89:('negative SHT slot0 branches to repair','0f8cd1000000'),
  0x51B9FF:('load ARM70/74/78 score weights','8b86700000008b8e740000008b9678000000'),
  0x51BA11:('store site80/anchor84/secondary88 weights','898780000000898f84000000899788000000'),
  0x53B211:('general-site bias KI23','03828c000000'),
  0x53B21B:('general-site scale KI27','0faf829c000000'),
  0x53B225:('general-site logical SHR12','c1e80c'),
  0x53B228:('ARM site weight runtime80','0faf8680000000'),
  0x53B281:('anchor distance reads model X','8b8794000000'),
  0x53B287:('anchor distance reads model X again','8b9794000000'),
  0x53B2B3:('anchor scale KI37','0faf82dc000000'),
  0x53B2BA:('anchor logical SHR12','c1e80c'),
  0x53B2BD:('ARM anchor weight runtime84','0faf8684000000'),
  0x53B398:('secondary score logical SHR12','c1e80c'),
  0x51453E:('alert category uses MDL acceleration18','83ba1800000000'),
  0x514571:('primary alert cooldown150','c78398f3500096000000'),
  0x514676:('secondary alert cooldown150','c783a4f3500096000000'),
  0x53B451:('military command cooldown8','c7838c00000008000000'),
  0x53B8C2:('group requires at least two units','83f902'),
  0x53B8E6:('group strength gate512','81fb00020000'),
  0x52A6DC:('hierarchy condition denominator4096','ba00100000'),
}
def verify(data):
    verify_creation(data)
    for va,(name,raw) in ANCHORS.items():
        off=retail_va_to_file(data,va)
        if data[off:off+len(bytes.fromhex(raw))]!=bytes.fromhex(raw):
            raise ValueError(f'opcode mismatch {va:08X}: {name}')
        print(f'PASS {va:08X} {name}: {raw}')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('retail_executable',type=Path)
    verify(p.parse_args().retail_executable.read_bytes())
