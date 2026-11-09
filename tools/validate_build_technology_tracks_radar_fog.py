#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, struct, subprocess, zipfile
from pathlib import Path


def sha(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1<<20),b''): h.update(chunk)
    return h.hexdigest()

def run(command: list[str]) -> list[str]:
    result=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(f"command failed ({result.returncode}): {' '.join(command)}\n{result.stdout}")
    return result.stdout.strip().splitlines()

def valid_pck(path: Path) -> bool:
    if not path.is_file() or path.stat().st_size < 0x200: return False
    data=path.read_bytes()[:12]
    return data[:4].lower()==b'pck\x00' and struct.unpack_from('<I',data,4)[0]==path.stat().st_size

def main()->int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--source',type=Path,required=True)
    ap.add_argument('--build',type=Path,required=True)
    ap.add_argument('--game-root',type=Path,required=True)
    ap.add_argument('--current-c-zip',type=Path,required=True)
    ap.add_argument('--current-gzf',type=Path,required=True)
    ap.add_argument('--binary',type=Path,required=True)
    ap.add_argument('--untouched-c-archive',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args()
    source=args.source.resolve(); build=args.build.resolve(); game=args.game_root.resolve()
    required_pcks=['LEVEL.PCK','Level00.pck','PATCH00.PCK','FILME.PCK','DATEN.PCK','ENGINE.PCK','GRAPHIK.PCK','MODELLE.PCK','SOUND.PCK']
    bad=[name for name in required_pcks if not valid_pck(game/name)]
    if bad: raise SystemExit('invalid stock PCK fixture(s): '+','.join(bad))
    with zipfile.ZipFile(args.current_c_zip) as archive:
        names=archive.namelist()
        if names != ['thandor.exe.c']: raise SystemExit('current C ZIP has unexpected members')
        c_bytes=archive.read(names[0])
    c_text=c_bytes.decode('utf-8',errors='replace')
    required_c=[
        'ArmyRuntimeClass_UpdateGroundMovementCollisionAndTrackAnimation',
        'GraphicsPrimitiveQueue_OffsetTextureCoordinates',
        'GameFactionRuntime_RecomputeProgressAndScoreMetrics',
        'ArmyPlacementMode_V344',
        'FactionRelationStateNibble_V344',
    ]
    missing=[token for token in required_c if token not in c_text]
    if missing: raise SystemExit('current C is missing G393 authority tokens: '+','.join(missing))
    contract=run(['python3',str(source/'tests/editor_integrated_compile_contract_tests.py'),'--source',str(source)])
    focused=run([str(build/'research_build_technology_tracks_radar_fog_tests'),str(game)])
    ctest=run(['ctest','--test-dir',str(build),'--output-on-failure'])
    record={
      'schema':'thandor-map-editor-g393-release-validation-v1',
      'status':'PASS',
      'authority':{
        'current_c_zip_sha256':sha(args.current_c_zip),
        'current_c_content_sha256':hashlib.sha256(c_bytes).hexdigest(),
        'current_c_content_bytes':len(c_bytes),
        'current_gzf_sha256':sha(args.current_gzf),
        'immutable_game_exe_sha256':sha(args.binary),
        'untouched_c_archive_sha256':sha(args.untouched_c_archive),
      },
      'stock_pck_header_validation':{name:'PASS' for name in required_pcks},
      'current_c_authority_tokens':required_c,
      'source_contract_output':contract,
      'focused_stock_output':focused,
      'ctest_output':ctest,
      'full_vulkan_app_local_status':'NOT_AVAILABLE: Vulkan headers/libraries absent; target installer must build before promotion',
    }
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
    print(json.dumps(record,indent=2,sort_keys=True))
    return 0
if __name__=='__main__': raise SystemExit(main())
