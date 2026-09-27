"""Tests for the reference decoder. Run from the repo root:

    python3 -m unittest discover -s tools
"""

import io
import subprocess
import sys
import unittest
from pathlib import Path

import itch_ref
import make_fixture

TOOLS = Path(__file__).resolve().parent

# Message lengths from the ITCH 5.0 specification's own tables, typed in
# separately so a mistake in BODIES can't agree with itself.
SPEC_LENGTHS = {
    "S": 12, "R": 39, "H": 25, "Y": 20, "L": 26, "V": 35, "W": 12, "K": 28, "J": 35, "h": 21,
    "A": 36, "F": 40, "E": 31, "C": 36, "X": 23, "D": 19, "U": 35, "P": 44, "Q": 40, "B": 19,
    "I": 50, "N": 20, "O": 48,
}


def frame(payload: bytes) -> bytes:
    return len(payload).to_bytes(2, "big") + payload


def run(data: bytes, every: int = 1, types: frozenset = frozenset()) -> str:
    out = io.StringIO()
    itch_ref.decode(io.BytesIO(data), out, every, set(types))
    return out.getvalue()


class Layouts(unittest.TestCase):
    def test_lengths_match_spec(self):
        self.assertEqual({t.decode(): n for t, n in itch_ref.LENGTHS.items()}, SPEC_LENGTHS)


class Decode(unittest.TestCase):
    def test_add_order_fields(self):
        payload = (b"A" + (7).to_bytes(2, "big") + (0).to_bytes(2, "big")
                   + (34_200_000_000_000).to_bytes(6, "big") + (123456).to_bytes(8, "big")
                   + b"B" + (100).to_bytes(4, "big") + b"AAPL    " + (1_525_000).to_bytes(4, "big"))
        self.assertEqual(run(frame(payload)),
                         "messages 1\ncount A 1\n"
                         "msg 0 A locate=7 tracking=0 timestamp=34200000000000 order_ref=123456 "
                         "side=B shares=100 stock=AAPL price=1525000\n")

    def test_reserved_byte_is_skipped(self):
        payload = make_fixture.pack(b"H")
        self.assertNotIn("reserved", run(frame(payload)))

    def test_sampling(self):
        data = b"".join(frame(make_fixture.pack(t)) for t in (b"S", b"D", b"D", b"S", b"B"))
        lines = [l for l in run(data, every=2).splitlines() if l.startswith("msg")]
        self.assertEqual([l.split()[1] for l in lines], ["0", "2", "4"])
        lines = [l for l in run(data, every=0, types={b"S"}).splitlines() if l.startswith("msg")]
        self.assertEqual([l.split()[1] for l in lines], ["0", "3"])

    def test_counts_sorted_by_type_byte(self):
        data = b"".join(frame(make_fixture.pack(t)) for t in (b"h", b"S", b"A"))
        counts = [l.split()[1] for l in run(data, every=0).splitlines() if l.startswith("count")]
        self.assertEqual(counts, ["A", "S", "h"])

    def test_alpha_escaping(self):
        self.assertEqual(itch_ref.format_alpha(b"AB C\\\x00  "), "AB\\x20C\\x5C\\x00")
        self.assertEqual(itch_ref.format_alpha(b"    "), "")


class FrameErrors(unittest.TestCase):
    def assertFrameError(self, data: bytes, fragment: str):
        with self.assertRaises(itch_ref.FrameError) as ctx:
            run(data)
        self.assertIn(fragment, str(ctx.exception))

    def test_truncated_prefix(self):
        self.assertFrameError(frame(make_fixture.pack(b"S")) + b"\x00", "truncated length prefix at message 1")

    def test_truncated_payload(self):
        self.assertFrameError(frame(make_fixture.pack(b"D"))[:-1], "frame says 19 bytes, file has 18")

    def test_wrong_length_for_type(self):
        self.assertFrameError(frame(make_fixture.pack(b"D") + b"\x00"), "type D is 19 bytes, frame is 20")

    def test_unknown_type(self):
        self.assertFrameError(frame(b"Z" + bytes(11)), "unknown type b'Z'")

    def test_empty_frame(self):
        self.assertFrameError(b"\x00\x00", "empty frame")


class Fixture(unittest.TestCase):
    def test_committed_fixture_is_current(self):
        result = subprocess.run([sys.executable, str(TOOLS / "make_fixture.py"), "--check"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_fixture_covers_every_type(self):
        _, ref = make_fixture.render()
        counted = {l.split()[1] for l in ref.splitlines() if l.startswith("count")}
        self.assertEqual(counted, set(SPEC_LENGTHS))


if __name__ == "__main__":
    unittest.main()
