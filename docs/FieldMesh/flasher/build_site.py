#!/usr/bin/env python3
"""Build the FieldMesh web flasher site (GitHub Pages) from one release.

Used by .github/workflows/fieldmesh-flasher.yml:
  build_site.py --release release.json --assets assets --libs libs --out site

release.json  output of `gh release view <tag> --json tagName,publishedAt,url`
assets/       the release files (DFU zips, *_full.bin)
libs/         unpacked npm packages: libs/esptool-js/package, libs/zipjs/package

Environment: GOATCOUNTER_CODE (optional) turns on the usage counter of the page.
"""
import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", required=True)
    ap.add_argument("--assets", required=True)
    ap.add_argument("--libs", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    rel = json.loads(Path(a.release).read_text())
    tag = rel["tagName"]
    assets = Path(a.assets)
    out = Path(a.out)
    if out.exists():
        shutil.rmtree(out)
    (out / "firmware").mkdir(parents=True)
    (out / "lib").mkdir()

    devices = []
    for dev in json.loads((HERE / "devices.json").read_text()):
        name = f'{dev["env"]}_FieldMesh{tag}{dev["suffix"]}'
        src = assets / name
        if not src.is_file():
            print(f"WARNING: {name} not in release {tag} - {dev['name']} left out", file=sys.stderr)
            continue
        shutil.copy2(src, out / "firmware" / name)
        entry = {k: v for k, v in dev.items() if k not in ("env", "suffix")}
        entry["file"] = f"firmware/{name}"
        entry["size"] = src.stat().st_size
        devices.append(entry)
    if not devices:
        sys.exit(f"ERROR: no device files found in release {tag}")

    info = {
        "tag": tag,
        "version": tag[1:] if tag.startswith("v") else tag,
        "published": rel.get("publishedAt", ""),
        "releaseUrl": rel.get("url", ""),
        "devices": devices,
    }
    # usage counter (repo variable GOATCOUNTER_CODE); without it the page counts nothing
    code = os.environ.get("GOATCOUNTER_CODE", "").strip().lower()
    if code:
        if not re.fullmatch(r"[a-z0-9-]{2,50}", code):
            sys.exit(f"ERROR: GOATCOUNTER_CODE '{code}' is not a GoatCounter code")
        info["counter"] = f"https://{code}.goatcounter.com/count"
    (out / "firmware" / "firmware.json").write_text(json.dumps(info, indent=2))

    for f in ("index.html", "nrf52_dfu_flasher.js", "nrf52_dfu_flasher.LICENSE.txt"):
        shutil.copy2(HERE / f, out / f)
    shutil.copytree(HERE / "fonts", out / "fonts")
    (out / "stats").mkdir()   # usage statistics, not linked anywhere; reads the 'stats' branch live
    shutil.copy2(HERE.parent / "stats" / "index.html", out / "stats" / "index.html")
    shutil.copytree(HERE.parent / "brand", out / "img")  # logo + contour lines, shared with the README
    libs = Path(a.libs)
    shutil.copy2(libs / "esptool-js/package/bundle.js", out / "lib/esptool.js")
    shutil.copy2(libs / "esptool-js/package/LICENSE", out / "lib/esptool.LICENSE.txt")
    shutil.copy2(libs / "zipjs/package/dist/zip.min.js", out / "lib/zip.min.js")
    shutil.copy2(libs / "zipjs/package/LICENSE", out / "lib/zip.LICENSE.txt")

    print(f"Flasher site for {tag}: " + ", ".join(d["name"] for d in devices))


if __name__ == "__main__":
    main()
