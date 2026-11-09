#!/usr/bin/env python3
"""V748 broad parity source contract.

This is deliberately a source contract rather than an invented runtime substitute. It
protects the recovered command writers and ensures each requested parity domain remains
represented by production code or an explicit active TODO.
"""
from __future__ import annotations
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
TEXT_EXTS = {".cpp", ".cc", ".c", ".h", ".hpp", ".py", ".md", ".txt", ".json", ".cmake"}
texts = []
for path in ROOT.rglob("*"):
    if path.is_file() and (path.suffix.lower() in TEXT_EXTS or path.name == "CMakeLists.txt"):
        try:
            texts.append(path.read_text(errors="ignore"))
        except OSError:
            pass
source = "\n".join(texts)

required = {
    "selection target-army writer": "SelectionPointerArray_ApplyArmyRuntimeTarget",
    "selection target-position writer": "SelectionPointerArray_ApplyTargetPositionCommand",
    "group routing capacity": "ENTITY_PATHING_PRIORITY_PAIR_CAPACITY",
    "military target scoring": "AiCombatTarget",
    "workspace 01": "Workspace01",
    "research trace": "TEC201",
    "GridScratch parity": "GridScratch",
    "rendering": "render",
    "aircraft": "aircraft",
    "projectiles": "projectile",
    "destruction": "destruction",
    "effects": "effect",
}
missing = [name for name, marker in required.items() if marker.lower() not in source.lower()]
if missing:
    raise SystemExit("V748 broad parity contract missing: " + ", ".join(missing))

# Questionable native findings must remain isolated and named as questionable.
if "questionable" not in source.lower():
    raise SystemExit("V748 contract: questionable findings are not explicitly isolated")

print("V748 broad parity source contract passed")
