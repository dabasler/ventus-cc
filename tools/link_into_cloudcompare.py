#!/usr/bin/env python3
"""Symlink the qVENTUS source into an existing CloudCompare Standard plugins tree.

Default behavior is non-destructive. Use --replace-copy to back up an existing
qVENTUS directory. Use --migrate-legacy to remove the old qVENTIS CMake entry
and a legacy symlink; copied legacy directories require --replace-copy too.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
from pathlib import Path
import re
import sys

OLD = "qVENTIS"
NEW = "qVENTUS"


def backup_dir(path: Path) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    dest = path.with_name(f"{path.name}.backup-{stamp}")
    if dest.exists() or dest.is_symlink():
        raise RuntimeError(f"Backup already exists: {dest}")
    path.rename(dest)
    print(f"Backed up {path.name} directory: {dest}")
    return dest


def migrate_old(repo: Path, standard: Path, allow_copy: bool) -> None:
    old = standard / OLD
    if old.is_symlink():
        target = old.resolve()
        # Never unlink a symlink installed from another VENTIS checkout.
        if target not in ((repo / OLD).resolve(), (repo / NEW).resolve()):
            raise RuntimeError(f"Refusing to change legacy symlink with unknown target: {old} -> {target}")
        old.unlink()
        print(f"Removed legacy {OLD} symlink: {old}")
    elif old.exists():
        if not old.is_dir():
            raise RuntimeError(f"Refusing to replace unexpected legacy path: {old}")
        if not allow_copy:
            raise RuntimeError(f"{old} is a copied directory; rerun with --migrate-legacy --replace-copy to back it up")
        backup_dir(old)


def register(standard: Path, migrate: bool = False) -> None:
    cmake = standard / "CMakeLists.txt"
    contents = cmake.read_text(encoding="utf-8")
    old_entry = rf"(?m)^[ \t]*add_subdirectory\s*\(\s*{OLD}\s*\)[ \t]*(?:\#[^\n]*)?\n?"
    new_entry = rf"(?m)^[ \t]*add_subdirectory\s*\(\s*{NEW}\s*\)"
    if re.search(old_entry, contents) and not migrate:
        raise RuntimeError(f"Legacy {OLD} CMake entry detected. Rerun with --migrate-legacy")
    if migrate:
        contents = re.sub(old_entry, "", contents)
        # Discard our old registration marker and replace it with a single new one.
        contents = re.sub(r"(?m)^# External VENTUS development plugin[ \t]*\n", "", contents)
    if not re.search(new_entry, contents):
        contents = contents.rstrip() + f"\n\n# External VENTUS development plugin\nadd_subdirectory( {NEW} )\n"
    if contents != cmake.read_text(encoding="utf-8"):
        cmake.write_text(contents, encoding="utf-8")
        print(f"Updated Standard plugin registration: {cmake}")
    else:
        print(f"{NEW} already registered in Standard/CMakeLists.txt")


def link(cc_root: Path, replace_copy: bool = False, migrate_legacy: bool = False) -> Path:
    repo = Path(__file__).resolve().parents[1]
    source = (repo / NEW).resolve()
    if not (source / "CMakeLists.txt").is_file() or not (repo / "core/src/Engine.cpp").is_file():
        raise RuntimeError(f"VENTUS repository is incomplete: {repo}")
    standard = cc_root.expanduser().resolve() / "plugins/core/Standard"
    cmake = standard / "CMakeLists.txt"
    if not cmake.is_file():
        raise RuntimeError(f"Missing CloudCompare Standard plugins CMakeLists.txt: {cmake}")

    destination = standard / NEW
    old = standard / OLD
    if old.is_symlink() or old.exists():
        if not migrate_legacy:
            raise RuntimeError(f"Legacy {OLD} installation exists at {old}. Use --migrate-legacy (and --replace-copy if copied).")
    if destination.is_symlink():
        if destination.resolve() != source:
            raise RuntimeError(f"{destination} points to another source: {destination.resolve()}")
    elif destination.exists():
        if not destination.is_dir():
            raise RuntimeError(f"Refusing to replace unexpected path: {destination}")
        if not replace_copy:
            raise RuntimeError(f"{destination} is a copied directory. Add --replace-copy to back it up.")
    # Ensure the existing CMake registration is safe to modify BEFORE changing links.
    if not migrate_legacy and re.search(r"(?m)^\s*add_subdirectory\s*\(\s*qVENTIS\s*\)", cmake.read_text(encoding="utf-8")):
        raise RuntimeError(f"Legacy {OLD} CMake entry detected. Use --migrate-legacy.")
    if migrate_legacy:
        migrate_old(repo, standard, replace_copy)
    if destination.is_symlink():
        pass  # Already correct.
    elif destination.exists():
        backup_dir(destination)
        destination.symlink_to(source, target_is_directory=True)
    else:
        destination.symlink_to(source, target_is_directory=True)
    register(standard, migrate=migrate_legacy)
    print(f"VENTUS source linked: {destination} -> {source}")
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cloudcompare", type=Path, help="CloudCompare source checkout")
    parser.add_argument("--replace-copy", action="store_true", help="back up an existing copied plugin directory")
    parser.add_argument("--migrate-legacy", action="store_true", help="migrate old qVENTIS registration and symlink")
    args = parser.parse_args()
    try:
        link(args.cloudcompare, args.replace_copy, args.migrate_legacy)
    except (RuntimeError, OSError) as error:
        print(f"VENTUS linking error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
