#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("native_audit", ROOT / "tools/audit_native_recording.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def record(tag, *words):
    data = struct.pack("<" + "I" * len(words), *words)
    return tag + struct.pack("<I", len(data)) + data


def fixture(duplicate=False, unknown=False):
    header = bytearray(192)
    header[:8] = b"THDTGR1\0"
    struct.pack_into("<II", header, 8, 1, 192)
    header[32:42] = "Probe".encode("utf-16-le")
    rows = [record(b"TICK", 1, *([0] * 11))]
    rows += [record(b"FACT", i, *([0] * 73)) for i in range(8)]
    rows += [record(b"ACTR", 17, *([0] * 40))]
    if duplicate:
        rows += [record(b"ACTR", 17, *([0] * 40))]
    if unknown:
        rows += [record(b"FUTR", 123)]
    rows += [record(b"ENDT", 1, len(rows) + 1, 2 if duplicate else 1, 8)]
    return bytes(header) + b"".join(rows)


class RecordingTests(unittest.TestCase):
    def audit(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "replay.tgr"
            path.write_bytes(data)
            return MODULE.audit(path)

    def test_complete_counts_and_unknown_fields(self):
        result = self.audit(fixture(unknown=True))
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["complete_frames"], 1)
        self.assertEqual(result["record_counts"]["FUTR"], 1)
        self.assertEqual(result["last_actor_raw_words_by_word0"]["17"], [17] + [0] * 40)

    def test_bad_counter(self):
        data = bytearray(fixture())
        struct.pack_into("<I", data, len(data) - 12, 99)
        self.assertIn("counter mismatch", self.audit(data)["errors"][0])

    def test_duplicate_actor(self):
        self.assertIn("duplicate ACTR", self.audit(fixture(duplicate=True))["errors"][0])

    def test_truncated_payload(self):
        self.assertIn("truncated payload", self.audit(fixture()[:-1])["errors"][0])

    def test_invalid_magic(self):
        self.assertIn("invalid magic", self.audit(b"invalid!" + fixture()[8:])["errors"])

    def test_missing_end(self):
        self.assertIn("before ENDT", self.audit(fixture()[:-24])["errors"][0])


if __name__ == "__main__":
    unittest.main()
