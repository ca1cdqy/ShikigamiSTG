#!/usr/bin/env python3
"""Assemble a platform-specific ShikigamiSTG SDK release archive."""

from __future__ import annotations

import argparse
import json
import shutil
import tempfile
import zipfile
from pathlib import Path


def copy_tree(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise FileNotFoundError(f"required directory not found: {source}")
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns(
        "__pycache__", "*.pyc", "node_modules"))


def platform_binaries(target_dir: Path, platform: str) -> list[Path]:
    if platform == "windows":
        names = ("shiki.dll", "shiki.lib", "th06.exe")
        binaries = [target_dir / name for name in names]
    elif platform == "linux":
        binaries = [target_dir / "libshiki.so", target_dir / "th06"]
    elif platform == "macos":
        binaries = [target_dir / "libshiki.dylib", target_dir / "th06"]
    else:
        raise ValueError(f"unsupported platform: {platform}")
    missing = [path for path in binaries if not path.is_file()]
    if missing:
        names = ", ".join(str(path) for path in missing)
        raise FileNotFoundError(f"release binaries not found: {names}")
    return binaries


def assemble(root: Path, project: Path, target_dir: Path, docs: Path,
             version: str, platform: str, architecture: str) -> None:
    bin_dir = root / "bin"
    bin_dir.mkdir(parents=True)
    for binary in platform_binaries(target_dir, platform):
        shutil.copy2(binary, bin_dir / binary.name)
    copy_tree(target_dir / "shaders", bin_dir / "shaders")
    copy_tree(project / "include", root / "include")
    copy_tree(project / "src", root / "src")
    copy_tree(project / "examples", root / "examples")
    copy_tree(project / "tools", root / "tools")
    copy_tree(docs, root / "docs")
    copy_tree(project / "assets" / "shaders", root / "assets" / "shaders")

    for name in ("README.md", "README.zh-CN.md", "CONTRIBUTING.md",
                 "CONTRIBUTING.zh-CN.md", "LICENSE", "Doxyfile",
                 ".clang-format", "xmake.lua"):
        source = project / name
        if not source.is_file():
            raise FileNotFoundError(f"required release file not found: {source}")
        shutil.copy2(source, root / name)

    (root / "VERSION").write_text(version + "\n", encoding="ascii")
    (root / "release.json").write_text(
        json.dumps({"architecture": architecture, "platform": platform,
                    "version": version}, separators=(",", ":"),
                   sort_keys=True) + "\n",
        encoding="utf-8")


def create_archive(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED,
                         compresslevel=9) as archive:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(source.parent))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path.cwd())
    parser.add_argument("--target-dir", type=Path, required=True)
    parser.add_argument("--docs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", choices=("windows", "linux", "macos"),
                        required=True)
    parser.add_argument("--arch", required=True)
    args = parser.parse_args()

    project = args.project.resolve()
    package_name = f"ShikigamiSTG-{args.version}-{args.platform}-{args.arch}"
    destination = args.output.resolve() / f"{package_name}.zip"
    with tempfile.TemporaryDirectory(prefix="shiki-release-") as temporary:
        package_root = Path(temporary) / package_name
        assemble(package_root, project, args.target_dir.resolve(),
                 args.docs.resolve(), args.version, args.platform, args.arch)
        create_archive(package_root, destination)
    print(destination)


if __name__ == "__main__":
    main()
