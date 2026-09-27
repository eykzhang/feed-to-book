#!/usr/bin/env python3
"""Reference decoder for Nasdaq TotalView-ITCH 5.0, the thing the C++ decoder diffs against.

Reads a framed ITCH file ([u16 big-endian length][payload], as in the Nasdaq
sample files and data/slices/), checks every frame's length against its
message type, and writes per-type counts plus a deterministic sample of fully
decoded messages.

    tools/itch_ref.py data/slices/01302019-first100000.itch
    tools/itch_ref.py data/slices/01302019-first100000.itch --every 1000
    tools/itch_ref.py data/slices/01302019-first100000.itch --type U --type C
    tools/itch_ref.py data/01302019.NASDAQ_ITCH50.gz --every 0     # counts only, full day

Output format, version 1. The C++ side should write the same bytes, so a plain
`diff` compares them. Lines, in this order:

    messages <total>
    count <type> <n>                 one per type seen, sorted by type byte
    msg <index> <type> <field>=<value> ...

A `msg` line is written for message index i (0-based, in file order) when
i % every == 0 (every > 0), or when its type was passed with --type. Fields
appear in the order the ITCH 5.0 spec lists them, header first: locate,
tracking, timestamp. Values:

    integers    unsigned decimal (share counts, order refs, match numbers)
    timestamp   nanoseconds since midnight, decimal
    prices      the raw fixed-point integer, undivided (Price(4) and Price(8) alike)
    alpha       trailing spaces stripped; bytes outside printable ASCII, and
                spaces or backslashes inside the value, written as \\xNN

Reserved fields are skipped. No trailing whitespace, lines end in \\n.
"""

import argparse
import gzip
import io
import signal
import struct
import sys
from collections import Counter

FORMAT_VERSION = 1

# Field kinds. Each maps to a struct code and how to print the unpacked value.
U8, U16, U32, U64 = "B", "H", "I", "Q"
TS = "6s"      # 48-bit big-endian nanoseconds since midnight
P4 = "I"       # Price(4): 4 implied decimals
P8 = "Q"       # Price(8): 8 implied decimals
SKIP = "skip"  # reserved byte, not printed


def alpha(n: int) -> str:
    return f"{n}s"


# ITCH 5.0 body layouts, after the common header (type, locate, tracking,
# timestamp). Field names follow the spec, snake_cased.
BODIES = {
    b"S": [("event_code", alpha(1))],
    b"R": [("stock", alpha(8)), ("market_category", alpha(1)), ("financial_status_indicator", alpha(1)),
           ("round_lot_size", U32), ("round_lots_only", alpha(1)), ("issue_classification", alpha(1)),
           ("issue_sub_type", alpha(2)), ("authenticity", alpha(1)), ("short_sale_threshold_indicator", alpha(1)),
           ("ipo_flag", alpha(1)), ("luld_reference_price_tier", alpha(1)), ("etp_flag", alpha(1)),
           ("etp_leverage_factor", U32), ("inverse_indicator", alpha(1))],
    b"H": [("stock", alpha(8)), ("trading_state", alpha(1)), ("reserved", SKIP), ("reason", alpha(4))],
    b"Y": [("stock", alpha(8)), ("reg_sho_action", alpha(1))],
    b"L": [("mpid", alpha(4)), ("stock", alpha(8)), ("primary_market_maker", alpha(1)),
           ("market_maker_mode", alpha(1)), ("market_participant_state", alpha(1))],
    b"V": [("level_1", P8), ("level_2", P8), ("level_3", P8)],
    b"W": [("breached_level", alpha(1))],
    b"K": [("stock", alpha(8)), ("ipo_quotation_release_time", U32),
           ("ipo_quotation_release_qualifier", alpha(1)), ("ipo_price", P4)],
    b"J": [("stock", alpha(8)), ("auction_collar_reference_price", P4), ("upper_auction_collar_price", P4),
           ("lower_auction_collar_price", P4), ("auction_collar_extension", U32)],
    b"h": [("stock", alpha(8)), ("market_code", alpha(1)), ("operational_halt_action", alpha(1))],
    b"A": [("order_ref", U64), ("side", alpha(1)), ("shares", U32), ("stock", alpha(8)), ("price", P4)],
    b"F": [("order_ref", U64), ("side", alpha(1)), ("shares", U32), ("stock", alpha(8)), ("price", P4),
           ("attribution", alpha(4))],
    b"E": [("order_ref", U64), ("executed_shares", U32), ("match_number", U64)],
    b"C": [("order_ref", U64), ("executed_shares", U32), ("match_number", U64), ("printable", alpha(1)),
           ("execution_price", P4)],
    b"X": [("order_ref", U64), ("cancelled_shares", U32)],
    b"D": [("order_ref", U64)],
    b"U": [("original_order_ref", U64), ("new_order_ref", U64), ("shares", U32), ("price", P4)],
    b"P": [("order_ref", U64), ("side", alpha(1)), ("shares", U32), ("stock", alpha(8)), ("price", P4),
           ("match_number", U64)],
    b"Q": [("shares", U64), ("stock", alpha(8)), ("cross_price", P4), ("match_number", U64),
           ("cross_type", alpha(1))],
    b"B": [("match_number", U64)],
    b"I": [("paired_shares", U64), ("imbalance_shares", U64), ("imbalance_direction", alpha(1)),
           ("stock", alpha(8)), ("far_price", P4), ("near_price", P4), ("current_reference_price", P4),
           ("cross_type", alpha(1)), ("price_variation_indicator", alpha(1))],
    b"N": [("stock", alpha(8)), ("interest_flag", alpha(1))],
    # Added to the spec in 2020, so absent from 2019 days.
    b"O": [("stock", alpha(8)), ("open_eligibility_status", alpha(1)), ("minimum_allowable_price", P4),
           ("maximum_allowable_price", P4), ("near_execution_price", P4), ("near_execution_time", U64),
           ("lower_price_range_collar", P4), ("upper_price_range_collar", P4)],
}

HEADER = [("locate", U16), ("tracking", U16), ("timestamp", TS)]


class Layout:
    """One message type: its struct, total length, and which fields print how."""

    def __init__(self, fields):
        self.names = []
        self.printers = []
        codes = [">c"]  # type byte, discarded after unpacking
        for name, kind in HEADER + fields:
            if kind == SKIP:
                codes.append("x")
                continue
            codes.append(kind)
            self.names.append(name)
            if kind == TS:
                self.printers.append(lambda v: str(int.from_bytes(v, "big")))
            elif kind.endswith("s"):
                self.printers.append(format_alpha)
            else:
                self.printers.append(str)
        self.struct = struct.Struct("".join(codes))
        self.length = self.struct.size

    def format(self, payload: bytes) -> str:
        values = self.struct.unpack(payload)[1:]
        return " ".join(f"{n}={p(v)}" for n, p, v in zip(self.names, self.printers, values))


def format_alpha(raw: bytes) -> str:
    out = []
    for b in raw.rstrip(b" "):
        if 0x21 <= b <= 0x7E and b != 0x5C:
            out.append(chr(b))
        else:
            out.append(f"\\x{b:02X}")
    return "".join(out)


LAYOUTS = {t: Layout(f) for t, f in BODIES.items()}
LENGTHS = {t: layout.length for t, layout in LAYOUTS.items()}


class FrameError(Exception):
    pass


def frames(f):
    """Yield (index, payload) for each frame in a binary stream."""
    index = 0
    while True:
        header = f.read(2)
        if not header:
            return
        if len(header) != 2:
            raise FrameError(f"truncated length prefix at message {index}")
        length = int.from_bytes(header, "big")
        payload = f.read(length)
        if len(payload) != length:
            raise FrameError(f"message {index}: frame says {length} bytes, file has {len(payload)}")
        yield index, payload
        index += 1


def decode(f, out, every: int, types: set) -> None:
    counts = Counter()
    samples = []
    for index, payload in frames(f):
        if not payload:
            raise FrameError(f"message {index}: empty frame")
        t = payload[:1]
        expected = LENGTHS.get(t)
        if expected is None:
            raise FrameError(f"message {index}: unknown type {t!r}")
        if len(payload) != expected:
            raise FrameError(f"message {index}: type {t.decode()} is {expected} bytes, frame is {len(payload)}")
        counts[t] += 1
        if (every and index % every == 0) or t in types:
            samples.append(f"msg {index} {t.decode()} {LAYOUTS[t].format(payload)}\n")

    out.write(f"messages {sum(counts.values())}\n")
    for t in sorted(counts):
        out.write(f"count {t.decode()} {counts[t]}\n")
    out.writelines(samples)


def open_input(path: str):
    raw = gzip.open(path, "rb") if path.endswith(".gz") else open(path, "rb")
    return io.BufferedReader(raw, buffer_size=1 << 22)


def main() -> None:
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)  # exit quietly when piped into head
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", help="framed ITCH file, raw or .gz")
    parser.add_argument("--every", type=int, default=1000,
                        help="write every Nth message in full, 0 for none (default 1000)")
    parser.add_argument("--type", action="append", default=[], metavar="T",
                        help="also write every message of this type; repeatable")
    parser.add_argument("-o", "--out", help="output file (default stdout)")
    args = parser.parse_args()

    if args.every < 0:
        sys.exit("--every must be 0 or positive")
    types = {t.encode() for t in args.type}
    unknown = types - LAYOUTS.keys()
    if unknown:
        sys.exit(f"unknown message types: {' '.join(sorted(t.decode() for t in unknown))}")

    out = open(args.out, "w", newline="\n") if args.out else sys.stdout
    try:
        with open_input(args.input) as f:
            decode(f, out, args.every, types)
    except FrameError as e:
        sys.exit(f"{args.input}: {e}")
    finally:
        if args.out:
            out.close()


if __name__ == "__main__":
    main()
