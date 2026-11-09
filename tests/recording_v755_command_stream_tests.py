#!/usr/bin/env python3
from pathlib import Path
import importlib.util
import json
import struct
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("audit", ROOT / "tools/audit_retail_command_stream.py")
AUDIT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(AUDIT)

def record(tag: bytes, payload: bytes) -> bytes:
    return tag + struct.pack("<I", len(payload)) + payload

def recording(with_command: bool) -> bytes:
    header = bytearray(192)
    header[:8] = b"THDTGR1\0"
    struct.pack_into("<III", header, 8, 1, 192, 0)
    header[32:32 + len("Fixture".encode("utf-16-le"))] = "Fixture".encode("utf-16-le")
    tick = struct.pack("<12I", 1, 1, 1, 256, 0, 0, 1, 0, 0, 2, 1, 0)
    payload = bytearray(record(b"TICK", tick))
    count = 2
    if with_command:
        payload += record(b"UCMD", struct.pack("<8I", 1, 1, 7, 1, 0xDE0, 3, 2, 1))
        count += 1
    payload += record(b"ENDT", struct.pack("<4I", 1, count, 0, 0))
    return bytes(header) + bytes(payload)

def main() -> int:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        raw = root / "replay.tgr"
        raw.write_bytes(recording(True))
        result = AUDIT.audit(raw)
        assert not result["errors"]
        assert result["complete_frames"] == 1
        assert result["user_command_count"] == 1
        assert result["target_position_command_count"] == 1
        archive = root / "capture.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("capture/replay.tgr", recording(False))
        zipped = AUDIT.audit(archive)
        assert not zipped["errors"]
        assert zipped["user_command_count"] == 0
        assert not zipped["selection_command_stream_available"]
        assert "cannot validate" in zipped["conclusion"]
    print("V755_RECORDING_COMMAND_STREAM PASS raw=1 zip_zero=1")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
