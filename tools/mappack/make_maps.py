#!/usr/bin/env python3
# T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
# Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
# SPDX-License-Identifier: GPL-3.0-or-later
# Made by N1k0droid. Like T-Deck SondeTracker? A star on
# https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

"""Builds the offline map for the T-Deck from OpenStreetMap data, entirely on your PC.

    python tools/mappack/make_maps.py tools/mappack/sicilia.json
    python tools/mappack/make_maps.py tools/mappack/sicilia.json --dry-run    # tile counts only

Needs Docker and Python 3 (Pillow recommended). Steps, all local:
1. Planetiler (Docker) turns each "source" into vector tiles (<workdir>/data/<source>.mbtiles),
   downloading the OpenStreetMap extract from Geofabrik plus coastline and Natural Earth data.
2. TileServer GL (Docker) draws them as 256 px PNG tiles with its built-in style.
3. Each "layer" fetches its zoom range over its area from that local server into <output>/z/x/y.png.
4. The result is checked (real, varied images) and an attribution file is added.
Copy the output folder to the SD card as /maps. Where a tile is missing, the firmware shows the
nearest lower zoom level enlarged, so layers can go from the whole world down to one region.

Layers are processed in order; a tile already present is kept, so put the most important layer first
when two layers share a zoom level. Map data (c) OpenStreetMap contributors, ODbL.
"""
import argparse
import collections
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import get_tiles  # noqa: E402

CONTAINER = "tdeck-maps-tileserver"
DEFAULTS = {
    "workdir": "mapwork",
    "output": "mapwork/maps",
    "planetiler_image": "ghcr.io/onthegomap/planetiler:latest",
    "tileserver_image": "maptiler/tileserver-gl:latest",
    "java_heap": "3g",
    "port": 8089,
    "workers": 4,
    "palette": True,
    "style": None,
}


def run(cmd, **kw):
    print("  $ " + " ".join(cmd))
    return subprocess.run(cmd, check=True, **kw)


def docker_ok():
    try:
        subprocess.run(["docker", "info"], check=True, capture_output=True)
        return True
    except (OSError, subprocess.CalledProcessError):
        return False


def build_source(cfg, name, src, data_dir, rebuild):
    out = os.path.join(data_dir, f"{name}.mbtiles")
    if os.path.exists(out) and not rebuild:
        print(f"[{name}] vector tiles already built: {out}")
        return
    print(f"[{name}] building vector tiles with Planetiler (area {src['area']}, maxzoom {src.get('maxzoom', 14)})")
    cmd = ["docker", "run", "--rm", "-e", f"JAVA_TOOL_OPTIONS=-Xmx{cfg['java_heap']}",
           "-v", f"{data_dir}:/data", cfg["planetiler_image"],
           "--download", f"--area={src['area']}", f"--maxzoom={src.get('maxzoom', 14)}",
           f"--output=/data/{name}.mbtiles", "--force"]
    if src.get("bounds"):
        cmd.append(f"--bounds={src['bounds']}")
    cmd += src.get("args", [])  # extra Planetiler options, e.g. "--exclude_layers=place"
    run(cmd)


def start_tileserver(cfg, name, data_dir):
    subprocess.run(["docker", "rm", "-f", CONTAINER], capture_output=True)
    run(["docker", "run", "-d", "--rm", "--name", CONTAINER, "-p", f"{cfg['port']}:8080",
         "-v", f"{data_dir}:/data", cfg["tileserver_image"], "--file", f"/data/{name}.mbtiles"],
        capture_output=True)
    base = f"http://localhost:{cfg['port']}"
    deadline = time.time() + 180
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(base + "/styles.json", timeout=5) as r:
                styles = json.loads(r.read())
            if styles:
                ids = [s["id"] for s in styles]
                style = cfg["style"] if cfg["style"] in ids else ids[0]
                print(f"[{name}] tile server ready, styles {ids}, using '{style}'")
                return f"{base}/styles/{style}/256/{{z}}/{{x}}/{{y}}.png"
        except Exception:
            pass
        time.sleep(2)
    stop_tileserver()
    sys.exit("the tile server did not start: see 'docker logs " + CONTAINER + "'")


def stop_tileserver():
    subprocess.run(["docker", "stop", CONTAINER], capture_output=True)


def layer_tiles(layer):
    z0, z1 = layer["zoom"]
    out = []
    for z in range(z0, z1 + 1):
        out += get_tiles.tiles_for_bbox(*layer["bbox"], z)
    return out


def write_center(cfg, out_dir):
    """center.txt: where the T-Deck map opens when no position is known (centre of the first layer,
    two zoom levels above its lowest so the whole area shows). Saves the firmware a slow folder listing."""
    first = cfg["layers"][0]
    s, w, n, e = first["bbox"]
    zoom = max(0, first["zoom"][0] - 2)
    with open(os.path.join(out_dir, "center.txt"), "w", encoding="utf-8") as f:
        f.write(f"{(s + n) / 2:.4f} {(w + e) / 2:.4f} {zoom}\n")


def check_output(out_dir):
    per_zoom = collections.Counter()
    size = 0
    hashes = collections.Counter()
    bad = []
    for root, _, files in os.walk(out_dir):
        for f in files:
            if not f.endswith(".png"):
                continue
            p = os.path.join(root, f)
            with open(p, "rb") as fh:
                b = fh.read()
            size += len(b)
            per_zoom[int(os.path.relpath(p, out_dir).split(os.sep)[0])] += 1
            if not b.startswith(get_tiles.PNG_MAGIC):
                bad.append(p)
            hashes[hashlib.md5(b).hexdigest()] += 1
    total = sum(per_zoom.values())
    print("\ntiles per zoom: " + ", ".join(f"{z}: {n}" for z, n in sorted(per_zoom.items())))
    print(f"total {total} tiles, {size / 1e6:.0f} MB, {len(hashes)} distinct images, {len(bad)} not PNG")
    if total and hashes.most_common(1)[0][1] > 0.9 * total:
        print("WARNING: almost all tiles are the same image: check the result before using it")
    if bad:
        print("WARNING: some files are not PNG images, e.g. " + bad[0])
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("config", help="JSON file with sources and layers (see tools/mappack/sicilia.json)")
    ap.add_argument("--dry-run", action="store_true", help="only print the tiles each layer needs")
    ap.add_argument("--rebuild", action="store_true", help="rebuild vector tiles even if already present")
    ap.add_argument("--center-only", action="store_true", help="only (re)write center.txt in the output folder")
    a = ap.parse_args()

    with open(a.config, encoding="utf-8") as f:
        cfg = dict(DEFAULTS, **json.load(f))
    workdir = os.path.abspath(cfg["workdir"])
    data_dir = os.path.join(workdir, "data")
    out_dir = os.path.abspath(cfg["output"])

    total = 0
    for layer in cfg["layers"]:
        n = len(layer_tiles(layer))
        total += n
        print(f"layer {layer['name']}: zoom {layer['zoom'][0]}-{layer['zoom'][1]}, source {layer['source']}, {n} tiles")
    print(f"all layers: {total} tiles, about {total * 12 // 1024 + 1} MB")
    if a.dry_run:
        return
    if a.center_only:
        os.makedirs(out_dir, exist_ok=True)
        write_center(cfg, out_dir)
        print(f"wrote {os.path.join(out_dir, 'center.txt')}")
        return
    if not docker_ok():
        sys.exit("Docker is not running or not installed")
    os.makedirs(data_dir, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)

    used = []
    for layer in cfg["layers"]:
        if layer["source"] not in used:
            used.append(layer["source"])
    for name in used:
        build_source(cfg, name, cfg["sources"][name], data_dir, a.rebuild)

    for name in used:
        url = start_tileserver(cfg, name, data_dir)
        try:
            for layer in [l for l in cfg["layers"] if l["source"] == name]:
                tiles = layer_tiles(layer)
                print(f"[{layer['name']}] rendering {len(tiles)} tiles (zoom {layer['zoom'][0]}-{layer['zoom'][1]})")
                t0 = time.time()
                done, failed = get_tiles.fetch_tiles(tiles, url, out_dir, workers=cfg["workers"], delay=0,
                                                     palette=cfg["palette"], progress_every=1000)
                print(f"[{layer['name']}] {done} new, {failed} failed, {time.time() - t0:.0f} s")
        finally:
            stop_tileserver()

    write_center(cfg, out_dir)
    with open(os.path.join(out_dir, "ATTRIBUTION.txt"), "w", encoding="utf-8") as f:
        f.write("Map data (c) OpenStreetMap contributors, available under the Open Database License (ODbL).\n"
                "Low zoom levels also use Natural Earth (public domain). Rendered with Planetiler and TileServer GL.\n")
    if check_output(out_dir):
        print(f"\nDone. Copy the folder '{out_dir}' to the root of the SD card and name it 'maps'.")


if __name__ == "__main__":
    main()
