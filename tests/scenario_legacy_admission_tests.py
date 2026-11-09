from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import audit_legacy_recording_parity as audit


def record(tag, words):
    payload = struct.pack('<' + 'I'*len(words), *words)
    return tag + struct.pack('<I', len(payload)) + payload


def frame(tick, duplicate=False, overflow=0, wrong_count=False):
    actor = [0]*41
    actor[0], actor[1], actor[2] = 7, 999, 0
    # Zero health/scale, arbitrary attachment count and signed coordinates are
    # not malformed; the legacy army interpretation must not reject these.
    actor[4] = 0xffffffff
    parts = [record(b'TICK', [tick, 1, 0, 307, 0, 0, 123, 0, 0, 2, 1, overflow]),
             record(b'ACTR', actor)]
    if duplicate:
        parts.append(record(b'ACTR', actor))
    parts.append(record(b'ENDT', [tick, len(parts)+1+(1 if wrong_count else 0), 2 if duplicate else 1, 0]))
    return b''.join(parts)


def scan(payload):
    header = b'THDTGR1\0'+struct.pack('<II', 1, 192)+bytes(176)
    stats = {}
    frames = list(audit.admitted_frames(header+payload, stats))
    return frames, stats


class AdmissionTests(unittest.TestCase):
    def test_valid_zero_values_and_signed_words(self):
        frames, stats = scan(frame(1))
        self.assertEqual(stats['accepted_frames'], 1)
        self.assertEqual(audit.signed(frames[0]['actors'][7][4]), -1)
        self.assertEqual(stats['excluded_frames'], 0)

    def test_bad_bounded_frame_excluded_next_frame_recovers(self):
        for bad in (frame(1, duplicate=True), frame(1, wrong_count=True), frame(1, overflow=1)):
            with self.subTest(bad=bad):
                frames, stats = scan(bad+frame(2))
                self.assertEqual([f['tick'][0] for f in frames], [2])
                self.assertEqual(stats['excluded_frames'], 1)

    def test_wrong_payload_and_unknown_tag_excluded(self):
        start = record(b'TICK', [1]*12)
        for bad in (record(b'ACTR', [7]), record(b'NOPE', [])):
            frames, stats = scan(start+bad+record(b'ENDT', [1, 3, 1, 0])+frame(2))
            self.assertEqual(len(frames), 1)
            self.assertEqual(stats['excluded_frames'], 1)

    def test_missing_end_discards_partial_frame(self):
        frames, stats = scan(frame(1)[:-24]+frame(2))
        self.assertEqual([f['tick'][0] for f in frames], [2])
        self.assertEqual(stats['excluded_frames'], 1)

    def test_rejected_tick_does_not_poison_later_valid_frames(self):
        frames, stats = scan(frame(1)+frame(999, wrong_count=True)+frame(2))
        self.assertEqual([f['tick'][0] for f in frames], [1, 2])
        self.assertEqual(stats['excluded_frames'], 1)

    def test_broken_boundary_does_not_guess_resynchronization(self):
        frames, stats = scan(frame(1)+b'ACTR'+struct.pack('<I', 0xffffffff)+frame(2))
        self.assertEqual([f['tick'][0] for f in frames], [1])
        self.assertIsNotNone(stats['stop_offset'])

    def test_truncated_frame_never_admitted(self):
        frames, stats = scan(frame(1)[:-1])
        self.assertEqual(frames, [])
        self.assertEqual(stats['excluded_frames'], 1)

    def test_bad_header(self):
        stats = {}
        self.assertEqual(list(audit.admitted_frames(b'bad', stats)), [])
        self.assertTrue(stats['errors'])


if __name__ == '__main__':
    unittest.main()
