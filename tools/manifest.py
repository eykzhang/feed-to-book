"""Read and write data/manifest.json, the record of what's in data/.

The manifest has two maps:
  days:   gzipped trading days as downloaded, keyed by filename
  slices: uncompressed prefixes of a day, keyed by filename
"""

import json
import os
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = REPO_ROOT / "data"
MANIFEST_PATH = DATA_DIR / "manifest.json"


def load() -> dict:
    if not MANIFEST_PATH.exists():
        return {"days": {}, "slices": {}}
    with MANIFEST_PATH.open() as f:
        return json.load(f)


def save(manifest: dict) -> None:
    DATA_DIR.mkdir(exist_ok=True)
    tmp = MANIFEST_PATH.with_suffix(".json.tmp")
    with tmp.open("w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")
    os.replace(tmp, MANIFEST_PATH)
