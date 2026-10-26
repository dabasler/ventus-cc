#!/usr/bin/env python3
"""Copy the qVENTUS plugin into CloudCompare (symlink workflow preferred).

Copied installations include the core solver under qVENTUS/core. Existing
source directories are never deleted: --force backs them up before replacing.
Use --migrate-legacy for old qVENTIS paths/CMake entries.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import sys

from link_into_cloudcompare import NEW, OLD, backup_dir, migrate_old, register


def install(cc_source: Path, force: bool = False, migrate_legacy: bool = False) -> Path:
    repo = Path(__file__).resolve().parents[1]
    standard = cc_source.expanduser().resolve() / "plugins/core/Standard"
    cmake = standard / "CMakeLists.txt"
    if not cmake.is_file():
        raise RuntimeError(f"CloudCompare Standard plugin CMake file not found: {cmake}")
    dest = standard / NEW
    old = standard / OLD
    if (old.exists() or old.is_symlink() or OLD in cmake.read_text(encoding="utf-8")) and not migrate_legacy:
        raise RuntimeError("Old qVENTIS installation/registration found; add --migrate-legacy")
    if dest.exists() or dest.is_symlink():
        if not force:
            raise RuntimeError(f"{dest} already exists. Use --force to back it up before replacing")
    if migrate_legacy:
        migrate_old(repo, standard, allow_copy=force)
    if dest.is_symlink():
        # Changing from symlink development to a copied installation is explicit.
        dest.unlink()
    elif dest.exists():
        backup_dir(dest)
    shutil.copytree(repo / NEW, dest)
    (dest / "core/src").mkdir(parents=True)
    (dest / "core/include/ventus").mkdir(parents=True)
    shutil.copy2(repo / "core/src/Engine.cpp", dest / "core/src/Engine.cpp")
    shutil.copy2(repo / "core/include/ventus/Engine.h", dest / "core/include/ventus/Engine.h")
    register(standard, migrate=migrate_legacy)
    return dest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cloudcompare", type=Path, help="CloudCompare 2.14+ source checkout")
    parser.add_argument("--force", action="store_true", help="back up and replace existing qVENTUS copy/link")
    parser.add_argument("--migrate-legacy", action="store_true", help="migrate old qVENTIS installation and CMake registration")
    args = parser.parse_args()
    try:
        dest = install(args.cloudcompare, args.force, args.migrate_legacy)
    except (RuntimeError, OSError) as exc:
        print(f"VENTUS install error: {exc}", file=sys.stderr)
        return 1
    print(f"VENTUS sources installed to {dest}")
    print("Reconfigure CloudCompare with -DPLUGIN_STANDARD_QVENTUS=ON and build.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
