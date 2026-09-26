#!/usr/bin/env python3
"""Download one Nasdaq TotalView-ITCH 5.0 sample day into data/.

Resumes a partial download, runs a full gzip integrity check, and records
the file's size and sha256 in data/manifest.json. Nasdaq's published .md5sum
files returned 404 when this was written, so the sha256 is our own record,
used to confirm later runs read the same bytes.

    tools/fetch_day.py                       # the default development day
    tools/fetch_day.py S121225-v50.txt.gz    # any file from the directory
"""

import argparse
import datetime
import hashlib
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request

import manifest

BASE_URL = "https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/"
DEFAULT_DAY = "01302019.NASDAQ_ITCH50.gz"
CHUNK = 1 << 20
DISK_MARGIN = 2 << 30  # leave at least 2 GiB free after the download


def remote_size(url: str) -> int:
    req = urllib.request.Request(url, method="HEAD")
    with urllib.request.urlopen(req, timeout=60) as resp:
        return int(resp.headers["Content-Length"])


def download(url: str, dest, total: int) -> None:
    part = dest.with_name(dest.name + ".part")
    have = part.stat().st_size if part.exists() else 0
    if have > total:
        sys.exit(f"{part} is larger than the remote file; delete it and retry")
    if have == total:
        part.rename(dest)
        return

    req = urllib.request.Request(url, headers={"Range": f"bytes={have}-"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        if have and resp.status != 206:
            sys.exit(f"server ignored the range request (HTTP {resp.status}); delete {part} and retry")
        with part.open("ab") as out:
            done = have
            last_pct = -1
            while chunk := resp.read(CHUNK):
                out.write(chunk)
                done += len(chunk)
                pct = done * 100 // total
                if pct != last_pct:
                    print(f"\r{dest.name}: {done / 2**30:.2f} / {total / 2**30:.2f} GiB ({pct}%)",
                          end="", flush=True)
                    last_pct = pct
    print()
    if part.stat().st_size != total:
        sys.exit(f"download incomplete: {part.stat().st_size} of {total} bytes; rerun to resume")
    part.rename(dest)


def sha256(path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(CHUNK * 8):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("day", nargs="?", default=DEFAULT_DAY, help=f"filename in the Nasdaq directory (default {DEFAULT_DAY})")
    args = parser.parse_args()

    url = BASE_URL + urllib.parse.quote(args.day)
    dest = manifest.DATA_DIR / args.day
    manifest.DATA_DIR.mkdir(exist_ok=True)

    m = manifest.load()
    if dest.exists() and args.day in m["days"]:
        print(f"{dest} already downloaded and verified")
        return

    if not dest.exists():
        total = remote_size(url)
        free = shutil.disk_usage(manifest.DATA_DIR).free
        if free < total + DISK_MARGIN:
            sys.exit(f"need {total / 2**30:.1f} GiB plus a 2 GiB margin, have {free / 2**30:.1f} GiB free")
        download(url, dest, total)

    print("checking gzip integrity (reads the whole file)...")
    if subprocess.run(["gzip", "-t", str(dest)]).returncode != 0:
        sys.exit(f"{dest} failed gzip -t; delete it and download again")

    print("hashing...")
    m["days"][args.day] = {
        "url": url,
        "bytes": dest.stat().st_size,
        "sha256": sha256(dest),
        "fetched": datetime.date.today().isoformat(),
    }
    manifest.save(m)
    print(f"{dest}: verified, recorded in {manifest.MANIFEST_PATH}")


if __name__ == "__main__":
    main()
