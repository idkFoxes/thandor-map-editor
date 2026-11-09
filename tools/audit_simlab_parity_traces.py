#!/usr/bin/env python3
"""Audit Simulation Lab/editor logs for the active V748 parity matrix."""
from __future__ import annotations
import argparse, json, re
from pathlib import Path

PATTERNS = {
    "owner_schedule": [r"owner", r"callback", r"insert|remove|unlink"],
    "ws05_order": [r"WS0?5|workspace.?05", r"site", r"route|command"],
    "faction_anchor_order": [r"anchor", r"route|command"],
    "ws07_group_order": [r"WS0?7|workspace.?07", r"group", r"target"],
    "ws03_fallback_order": [r"WS0?3|workspace.?03", r"fallback", r"target|route"],
    "tec201": [r"TEC.?201|technology.?201", r"WS0?0|WS12|WS13|research|purchase|complete"],
    "military_scoring": [r"military|combat", r"score|candidate", r"target"],
    "workspace01": [r"WS0?1|workspace.?01", r"admit|root|capacity|count"],
    "group_pathing": [r"group|overlap", r"route|path", r"priority|32"],
    "factory_orientation": [r"factory", r"heading|orientation|exit|spawn"],
    "rendering": [r"render|vulkan|shadow|draw"],
    "aircraft": [r"aircraft|pad|flight|landing"],
    "projectiles": [r"projectile|shot|impact|beam"],
    "destruction": [r"destroy|destruction|dismantl|ruin"],
    "effects": [r"effect|emitter|particle|sound"],
}

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+", type=Path)
    ap.add_argument("--json", dest="json_path", type=Path)
    args=ap.parse_args()
    text="\n".join(p.read_text(errors="ignore") for p in args.paths if p.is_file())
    result={}
    for name, patterns in PATTERNS.items():
        hits=[]
        for pat in patterns:
            matches=list(re.finditer(pat,text,re.I))
            hits.append({"pattern":pat,"count":len(matches)})
        result[name]={"complete_pattern_coverage":all(h["count"] for h in hits),"patterns":hits}
    result["all_domains_covered"]=all(v["complete_pattern_coverage"] for v in result.values())
    payload=json.dumps(result,indent=2)
    print(payload)
    if args.json_path:
        args.json_path.write_text(payload+"\n")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
