"""Shared checks for contracts that survive editor releases."""
import re
from pathlib import Path


def require_editor_version_at_least(root: Path, minimum: int) -> int:
    header = (root / "include/thandor/editor_version.hpp").read_text(encoding="utf-8")
    match = re.search(r'kEditorVersion\s*=\s*"(?:V|\d+\.\d+\.)(\d+)"', header)
    if match is None:
        raise AssertionError("editor version is missing or malformed")
    version = int(match.group(1))
    if version < minimum:
        raise AssertionError(f"this contract requires V{minimum} or later; found V{version}")
    return version
