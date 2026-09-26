#!/usr/bin/env python3
"""Write an uncompressed prefix of a gzipped ITCH 5.0 day to data/slices/.

A slice is always a prefix of the day: it starts at the first message, so it
contains the stock directory and every add before any execute, cancel, or
replace that refers to it. A window cut from the middle would not.

The output is byte-identical to the start of the decompressed day, framed as
[u16 big-endian length][payload], and ends on a frame boundary.

    tools/slice.py --count 100000            # first 100k messages
    tools/slice.py --until 10:00             # every message before 10:00:00 ET
    tools/slice.py --until 10:00 --day S121225-v50.txt.gz

Message count and last timestamp go into data/manifest.json.
"""

import argparse
import gzip
import io
import os
import sys

import manifest

DEFAULT_DAY = "01302019.NASDAQ_ITCH50.gz"
NS_PER_SEC = 1_000_000_000
# Every ITCH 5.0 message starts with type(1) locate(2) tracking(2) timestamp(6),
# the timestamp being big-endian nanoseconds since midnight.
TS_OFFSET = 5
TS_END = 11


def parse_clock(s: str) -> int:
    parts = [int(p) for p in s.split(":")]
    if not 2 <= len(parts) <= 3:
        raise argparse.ArgumentTypeError("expected HH:MM or HH:MM:SS")
    h, m, sec = (parts + [0])[:3]
    return ((h * 60 + m) * 60 + sec) * NS_PER_SEC


def format_clock(ns: int) -> str:
    sec, frac = divmod(ns, NS_PER_SEC)
    return f"{sec // 3600:02d}:{sec // 60 % 60:02d}:{sec % 60:02d}.{frac:09d}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--day", default=DEFAULT_DAY, help=f"gzipped day in data/ (default {DEFAULT_DAY})")
    stop = parser.add_mutually_exclusive_group(required=True)
    stop.add_argument("--count", type=int, help="number of messages to keep")
    stop.add_argument("--until", type=parse_clock, help="keep messages with timestamp before HH:MM[:SS]")
    parser.add_argument("--out", help="output filename in data/slices/ (default derived from the arguments)")
    args = parser.parse_args()

    src = manifest.DATA_DIR / args.day
    if not src.exists():
        sys.exit(f"{src} not found; run tools/fetch_day.py first")

    stem = args.day.split(".")[0]
    if args.count is not None:
        if args.count <= 0:
            sys.exit("--count must be positive")
        cut = f"count={args.count}"
        default_name = f"{stem}-first{args.count}.itch"
    else:
        clock = format_clock(args.until)[:8]
        cut = f"until={clock}"
        default_name = f"{stem}-until{clock.replace(':', '')}.itch"
    name = args.out or default_name

    out_dir = manifest.DATA_DIR / "slices"
    out_dir.mkdir(exist_ok=True)
    dest = out_dir / name
    tmp = dest.with_name(dest.name + ".part")

    messages = 0
    last_ts = None
    with gzip.open(src, "rb") as gz, \
         io.BufferedReader(gz, buffer_size=1 << 22) as f, \
         tmp.open("wb", buffering=1 << 22) as out:
        while args.count is None or messages < args.count:
            header = f.read(2)
            if not header:
                break  # clean end of day
            if len(header) != 2:
                sys.exit(f"truncated length prefix after {messages} messages")
            length = int.from_bytes(header, "big")
            payload = f.read(length)
            if length < TS_END or len(payload) != length:
                sys.exit(f"bad frame after {messages} messages: length {length}, read {len(payload)}")
            ts = int.from_bytes(payload[TS_OFFSET:TS_END], "big")
            if args.until is not None and ts >= args.until:
                break
            out.write(header)
            out.write(payload)
            messages += 1
            last_ts = ts
            if messages % 5_000_000 == 0:
                print(f"\r{messages:,} messages, at {format_clock(ts)}", end="", flush=True)
    print()

    if args.count is not None and messages < args.count:
        tmp.unlink()
        sys.exit(f"day has only {messages:,} messages, fewer than --count {args.count:,}")

    os.replace(tmp, dest)
    m = manifest.load()
    m["slices"][name] = {
        "source": args.day,
        "cut": cut,
        "messages": messages,
        "bytes": dest.stat().st_size,
        "last_timestamp_ns": last_ts,
        "last_timestamp": format_clock(last_ts) if last_ts is not None else None,
    }
    manifest.save(m)
    print(f"{dest}: {messages:,} messages, {dest.stat().st_size / 2**20:,.1f} MiB, last at "
          f"{m['slices'][name]['last_timestamp']}")


if __name__ == "__main__":
    main()
