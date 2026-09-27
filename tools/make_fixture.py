#!/usr/bin/env python3
"""Write tests/data/all_types.itch and its reference output, all_types.ref.

The fixture is synthetic: one message of every ITCH 5.0 type, then a few edge
cases, framed like a sample file. It exists because CI has no market data and
the 2019 development day never sends W, K, h, B, N, or O.

Integer fields get values whose bytes are all distinct and nonzero, so a
missing or partial byte swap produces a visibly wrong number. The edge cases
set the high bit of share, price, and order-ref fields (catches signed types),
put a timestamp at 23:59:59.999999999 (all six bytes significant), and put
non-printable and space bytes in alpha fields (exercises escaping).

    tools/make_fixture.py            # rewrite both files
    tools/make_fixture.py --check    # exit 1 if either file is stale
"""

import argparse
import io
import struct
import sys
from pathlib import Path

import itch_ref
from itch_ref import BODIES, HEADER, LAYOUTS, SKIP, TS

OUT_DIR = Path(__file__).resolve().parent.parent / "tests" / "data"
FIXTURE = OUT_DIR / "all_types.itch"
REFERENCE = OUT_DIR / "all_types.ref"

LAST_NS_OF_DAY = 86_400 * 1_000_000_000 - 1


def distinct(width: int, seed: int) -> int:
    """An integer of `width` bytes: seed, seed+1, ... big-endian, high bit clear."""
    return int.from_bytes(bytes((seed + i) & 0x7F or 1 for i in range(width)), "big")


def default_value(kind: str, seed: int):
    if kind == TS:
        return distinct(6, seed).to_bytes(6, "big")
    if kind.endswith("s"):
        width = int(kind[:-1])
        return (b"ZVZZT" if width == 8 else b"QRSTUVWX")[:width].ljust(width)
    return distinct(struct.calcsize(">" + kind), seed)


def pack(t: bytes, overrides: dict | None = None, seed: int = 0x11) -> bytes:
    values = []
    for i, (name, kind) in enumerate(HEADER + BODIES[t]):
        if kind == SKIP:
            continue
        v = default_value(kind, seed + 7 * i)
        if overrides and name in overrides:
            v = overrides[name]
            if kind == TS:
                v = v.to_bytes(6, "big")
        values.append(v)
    return LAYOUTS[t].struct.pack(t, *values)


def messages() -> list[bytes]:
    msgs = [pack(t, seed=0x11 + 3 * n) for n, t in enumerate(sorted(BODIES))]
    # Unsigned edge cases: every field that is signed-by-mistake goes negative here.
    msgs.append(pack(b"A", {"order_ref": 0xFFFFFFFFFFFFFFFF, "shares": 0xFFFFFFFF, "price": 0x80000000,
                            "timestamp": LAST_NS_OF_DAY, "locate": 0xFFFF, "side": b"S"}))
    msgs.append(pack(b"U", {"original_order_ref": 0x8000000000000000, "new_order_ref": 1,
                            "shares": 0x80000000, "price": 0xFFFFFFFF, "timestamp": 0}))
    msgs.append(pack(b"Q", {"shares": 0x8000000000000001}))
    # Alpha escaping: an internal space, a backslash, a NUL, and a trailing-space-only field.
    msgs.append(pack(b"F", {"stock": b"BRK A\\\x00 ", "attribution": b"    "}))
    return msgs


def render() -> tuple[bytes, str]:
    framed = b"".join(len(m).to_bytes(2, "big") + m for m in messages())
    out = io.StringIO()
    itch_ref.decode(io.BytesIO(framed), out, every=1, types=set())
    return framed, out.getvalue()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="verify the committed files instead of writing")
    args = parser.parse_args()

    framed, ref = render()
    if args.check:
        stale = [p.name for p, want in ((FIXTURE, framed), (REFERENCE, ref.encode()))
                 if not p.exists() or p.read_bytes() != want]
        if stale:
            sys.exit(f"stale: {', '.join(stale)}; run tools/make_fixture.py")
        return

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    FIXTURE.write_bytes(framed)
    REFERENCE.write_bytes(ref.encode())
    print(f"{FIXTURE}: {len(framed)} bytes; {REFERENCE}: {ref.count(chr(10))} lines")


if __name__ == "__main__":
    main()
