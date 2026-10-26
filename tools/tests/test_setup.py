"""Unit tests for the CloudCompare development installer (no Qt required)."""
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
TOOLS = REPO / "tools"


def mock_cc(base: Path, body: str = "# Standard plugins\n") -> tuple[Path, Path]:
    cc = base / "CloudCompare"
    standard = cc / "plugins/core/Standard"
    standard.mkdir(parents=True)
    (standard / "CMakeLists.txt").write_text(body, encoding="utf-8")
    return cc, standard


def call(script: str, cc: Path, *flags: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(TOOLS / script), str(cc), *flags], text=True, capture_output=True)


class SetupTests(unittest.TestCase):
    def test_fresh_link_and_repeat(self):
        with tempfile.TemporaryDirectory() as t:
            cc, standard = mock_cc(Path(t))
            for _ in range(2):
                result = call("link_into_cloudcompare.py", cc)
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((standard / "qVENTUS").is_symlink())
            content = (standard / "CMakeLists.txt").read_text()
            self.assertEqual(content.count("add_subdirectory( qVENTUS )"), 1)
            self.assertNotIn("qVENTIS", content)

    def test_migrate_old_symlink_and_registration(self):
        with tempfile.TemporaryDirectory() as t:
            cc, standard = mock_cc(Path(t), "add_subdirectory( qVENTIS )\n")
            (standard / "qVENTIS").symlink_to(REPO / "qVENTIS", target_is_directory=True)
            result = call("link_into_cloudcompare.py", cc, "--migrate-legacy")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((standard / "qVENTIS").is_symlink())
            self.assertTrue((standard / "qVENTUS").is_symlink())
            self.assertNotIn("qVENTIS", (standard / "CMakeLists.txt").read_text())
            self.assertIn("add_subdirectory( qVENTUS )", (standard / "CMakeLists.txt").read_text())

    def test_copy_requires_backup(self):
        with tempfile.TemporaryDirectory() as t:
            cc, standard = mock_cc(Path(t), "add_subdirectory( qVENTIS )\n")
            old = standard / "qVENTIS"
            old.mkdir()
            (old / "valuable.cpp").write_text("keep me")
            rejected = call("link_into_cloudcompare.py", cc, "--migrate-legacy")
            self.assertNotEqual(rejected.returncode, 0)
            self.assertTrue((old / "valuable.cpp").is_file())
            accepted = call("link_into_cloudcompare.py", cc, "--migrate-legacy", "--replace-copy")
            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            backups = list(standard.glob("qVENTIS.backup-*"))
            self.assertEqual(len(backups), 1)
            self.assertEqual((backups[0] / "valuable.cpp").read_text(), "keep me")

    def test_unknown_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as t:
            cc, standard = mock_cc(Path(t), "add_subdirectory( qVENTIS )\n")
            (standard / "qVENTIS").symlink_to(Path(t) / "other-source")
            result = call("link_into_cloudcompare.py", cc, "--migrate-legacy")
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue((standard / "qVENTIS").is_symlink())

    def test_copy_install(self):
        with tempfile.TemporaryDirectory() as t:
            cc, standard = mock_cc(Path(t))
            result = call("install_into_cloudcompare.py", cc)
            self.assertEqual(result.returncode, 0, result.stderr)
            plugin = standard / "qVENTUS"
            self.assertTrue((plugin / "qVENTUS.cpp").is_file())
            self.assertTrue((plugin / "core/src/Engine.cpp").is_file())
            self.assertTrue((plugin / "core/include/ventus/Engine.h").is_file())
            self.assertIn("add_subdirectory( qVENTUS )", (standard / "CMakeLists.txt").read_text())


if __name__ == "__main__":
    unittest.main()
