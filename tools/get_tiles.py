#!/usr/bin/env python3
# T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
# Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
# SPDX-License-Identifier: GPL-3.0-or-later
# Made by N1k0droid. Like T-Deck SondeTracker? A star on
# https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

"""Fetches map tiles for the offline map of the firmware.

Creates <out>/<zoom>/<x>/<y>.png; copy the folder to the SD card as /maps.
Normally called by tools/mappack/make_maps.py, which renders the tiles locally from OpenStreetMap
data. It can also be used alone with any tile server whose terms allow offline use.

Example (30 km around Trapani, zoom 8 to 14):
    python tools/get_tiles.py --url "https://example.com/{z}/{x}/{y}.png?key=KEY" \\
        --lat 38.0176 --lon 12.5365 --radius-km 30 --zoom 8-14 --out maps
Example (all of Sicily at overview zooms):
    python tools/get_tiles.py --url "https://example.com/{z}/{x}/{y}.png?key=KEY" \\
        --bbox 36.6,12.3,38.35,15.7 --zoom 6-12 --out maps

--url is required: a tile URL template with {z}, {x}, {y}. The public OpenStreetMap tile servers
do not allow bulk downloads: they answer scripts like this one with an "Access blocked" image.
For remote servers the script stops when the first tiles are all the same image, which is how such
a refusal looks; the check is skipped for a local server (localhost), where sea tiles legitimately repeat.
"""
import argparse
import concurrent.futures
import hashlib
import io
import math
import os
import sys
import threading
import time
import urllib.parse
import urllib.request

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
MAX_ZOOM = 18

try:
    from PIL import Image
except ImportError:
    Image = None


def tile_xy(lat, lon, z):
    n = 1 << z
    lat = max(min(lat, 85.0511), -85.0511)
    x = int((lon + 180.0) / 360.0 * n)
    phi = math.radians(lat)
    y = int((1.0 - math.log(math.tan(phi) + 1.0 / math.cos(phi)) / math.pi) / 2.0 * n)
    return min(max(x, 0), n - 1), min(max(y, 0), n - 1)


def tiles_for(lat, lon, radius_km, z):
    dlat = radius_km / 111.32
    dlon = radius_km / (111.32 * math.cos(math.radians(lat)))
    x0, y0 = tile_xy(lat + dlat, lon - dlon, z)
    x1, y1 = tile_xy(lat - dlat, lon + dlon, z)
    return [(z, x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)]


def tiles_for_bbox(south, west, north, east, z):
    x0, y0 = tile_xy(north, west, z)
    x1, y1 = tile_xy(south, east, z)
    return [(z, x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)]


def tile_path(out, z, x, y):
    return os.path.join(out, str(z), str(x), f"{y}.png")


def to_palette(data):
    """8-bit palette PNG: about half the size and faster to decode on the T-Deck."""
    img = Image.open(io.BytesIO(data)).convert("RGB")
    buf = io.BytesIO()
    img.quantize(colors=256, method=Image.Quantize.MEDIANCUT).save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def is_local(url):
    host = urllib.parse.urlparse(url.replace("{z}", "0").replace("{x}", "0").replace("{y}", "0")).hostname or ""
    return host in ("localhost", "127.0.0.1", "::1")


def fetch_tiles(tiles, url, out, workers=1, delay=0.25, palette=False, user_agent="t-deck-sonde-tracker-tiles/1.0",
                progress_every=200):
    """Downloads the tiles not already in out. Returns (done, failed)."""
    if palette and Image is None:
        print("warning: Pillow not installed, tiles kept as they come (pip install pillow)")
        palette = False
    todo = [t for t in tiles if not os.path.exists(tile_path(out, *t))]
    check_refusal = not is_local(url)
    lock = threading.Lock()
    state = {"done": 0, "failed": 0, "first": [], "abort": None}

    def one(t):
        if state["abort"]:
            return
        z, x, y = t
        path = tile_path(out, z, x, y)
        try:
            req = urllib.request.Request(url.format(z=z, x=x, y=y), headers={"User-Agent": user_agent})
            with urllib.request.urlopen(req, timeout=60) as r:
                data = r.read()
            if not data.startswith(PNG_MAGIC):
                raise ValueError("not a PNG image")
            if check_refusal:
                with lock:
                    if len(state["first"]) < 6:
                        state["first"].append(hashlib.md5(data).hexdigest())
                        if len(state["first"]) == 6 and len(set(state["first"])) == 1:
                            state["abort"] = ("the server sent the same image for the first tiles: it is refusing "
                                              "the download (delete the tiles saved so far)")
                            return
            if palette:
                data = to_palette(data)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path + ".tmp", "wb") as f:
                f.write(data)
            os.replace(path + ".tmp", path)
            with lock:
                state["done"] += 1
        except Exception as e:  # keep going: one bad tile is only a lower-zoom fallback on the map
            with lock:
                state["failed"] += 1
            print(f"  failed {z}/{x}/{y}: {e}")
        with lock:
            n = state["done"] + state["failed"]
            if n % progress_every == 0:
                print(f"  {n}/{len(todo)}")
        if delay:
            time.sleep(delay)

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, workers)) as ex:
        list(ex.map(one, todo))
    if state["abort"]:
        sys.exit(state["abort"])
    return state["done"], state["failed"]


def parse_zoom(text):
    zs = text.split("-")
    z0, z1 = int(zs[0]), int(zs[-1])
    if z0 < 0 or z1 > MAX_ZOOM or z0 > z1:
        sys.exit(f"zoom must be within 0..{MAX_ZOOM}")
    return z0, z1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lat", type=float)
    ap.add_argument("--lon", type=float)
    ap.add_argument("--bbox", help="south,west,north,east in degrees, instead of --lat/--lon/--radius-km")
    ap.add_argument("--radius-km", type=float, default=20.0)
    ap.add_argument("--zoom", default="8-14", help="single level or range, e.g. 12 or 8-14")
    ap.add_argument("--out", default="maps")
    ap.add_argument("--url", required=True, help="tile URL template with {z} {x} {y}")
    ap.add_argument("--delay", type=float, default=0.25, help="seconds between requests (per worker)")
    ap.add_argument("--workers", type=int, default=1, help="parallel requests (use more only on a local server)")
    ap.add_argument("--palette", action="store_true", help="save 8-bit palette PNG (needs Pillow)")
    ap.add_argument("--max-tiles", type=int, default=3000)
    ap.add_argument("--user-agent", default="t-deck-sonde-tracker-tiles/1.0 (personal offline map)")
    ap.add_argument("--dry-run", action="store_true", help="only print how many tiles would be downloaded")
    a = ap.parse_args()

    z0, z1 = parse_zoom(a.zoom)
    bbox = [float(v) for v in a.bbox.split(",")] if a.bbox else None
    if bbox is None and (a.lat is None or a.lon is None):
        sys.exit("give --lat and --lon, or --bbox")

    todo = []
    for z in range(z0, z1 + 1):
        t = tiles_for_bbox(*bbox, z) if bbox else tiles_for(a.lat, a.lon, a.radius_km, z)
        print(f"zoom {z}: {len(t)} tiles")
        todo += t
    missing = [t for t in todo if not os.path.exists(tile_path(a.out, *t))]
    print(f"total {len(todo)} tiles, {len(missing)} to download, about {len(missing) * 15 // 1024 + 1} MB")
    if a.dry_run:
        return
    if len(missing) > a.max_tiles:
        sys.exit(f"more than --max-tiles {a.max_tiles}: reduce the area or the zoom range")
    done, failed = fetch_tiles(missing, a.url, a.out, a.workers, a.delay, a.palette, a.user_agent)
    print(f"downloaded {done}, failed {failed}. Copy '{a.out}' to the SD card as /maps")


if __name__ == "__main__":
    main()
