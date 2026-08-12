#!/usr/bin/env python3
"""Package the TH06 web asset bundle with Emscripten's file packager.

The browser build needs a virtual filesystem containing the extracted game
assets, the generated shaders, and a CJK font. Repacking these on every
xmake build is slow, so this script generates the bundle once, outside the
build flow:

    python tools/package_wasm_data.py --assets <assets-dir> [options]

It writes <output>/th06.data plus a loader <output>/th06.data.js that the
wasm build embeds via --pre-js; th06.data is fetched at runtime next to
th06.html. The data files are original-game assets; keep them out of any
public distribution.

Options:
  --assets DIR   Extracted game assets folder (mounted at /assets).
  --shaders DIR  Generated shader folder (mounted at /shaders).
  --font FILE    A CJK font file (mounted at /fonts).
  --output DIR   Output directory (default: build/wasm/wasm32/release).
  --name NAME    Data base name (default: th06).
"""

import argparse
import os
import shutil
import subprocess
import sys


def find_file_packager():
    root = os.environ.get("EMSCRIPTEN_ROOT")
    candidates = []
    if root:
        candidates.append(os.path.join(root, "tools", "file_packager.py"))
    for base in (os.path.expanduser("~"), os.path.expanduser("~/Documents")):
        candidates.append(os.path.join(
            base, "emsdk", "upstream", "emscripten", "tools", "file_packager.py"))
    located = shutil.which("file_packager.py")
    if located:
        candidates.append(located)
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    raise SystemExit(
        "file_packager.py not found. Install the Emscripten SDK or set "
        "EMSCRIPTEN_ROOT to its upstream/emscripten directory.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", required=True,
                        help="extracted game assets folder (mounted at /assets)")
    parser.add_argument("--shaders",
                        help="generated shader folder (mounted at /shaders)")
    parser.add_argument("--font", help="CJK font file (mounted at /fonts)")
    parser.add_argument(
        "--output",
        default=os.path.join("build", "wasm", "wasm32", "release"),
        help="output directory (default: build/wasm/wasm32/release)")
    parser.add_argument("--name", default="th06",
                        help="data base name (default: th06)")
    args = parser.parse_args()

    if not os.path.isdir(args.assets):
        raise SystemExit(f"assets folder not found: {args.assets}")
    os.makedirs(args.output, exist_ok=True)

    data_path = os.path.join(args.output, args.name + ".data")
    js_path = os.path.join(args.output, args.name + ".data.js")
    file_packager = find_file_packager()

    command = [sys.executable, file_packager, data_path,
               "--preload", args.assets + "@/assets",
               "--js-output=" + js_path]
    if args.shaders:
        if not os.path.isdir(args.shaders):
            raise SystemExit(f"shaders folder not found: {args.shaders}")
        command += ["--preload", args.shaders + "@/shaders"]
    if args.font:
        if not os.path.isfile(args.font):
            raise SystemExit(f"font file not found: {args.font}")
        command += ["--preload", args.font + "@/fonts"]

    subprocess.run(command, check=True)
    size_mib = os.path.getsize(data_path) / (1024 * 1024)
    print(f"Wrote {data_path} ({size_mib:.1f} MiB) and {js_path}")
    print("Build the browser target with: xmake build th06")


if __name__ == "__main__":
    main()