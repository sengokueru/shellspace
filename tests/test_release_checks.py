"""Regression tests for the release gates; IR fixtures are generated in isolation."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "IR"))
import prepare_ir
from ir_manifest import EXPECTED
spec = importlib.util.spec_from_file_location("release_version", ROOT / "scripts/release_version.py")
version_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version_module)


class ReleaseVersionTests(unittest.TestCase):
    def test_github_environment_export(self):
        expected = version_module.read_version((ROOT / "ShellSpace/CMakeLists.txt").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="shellspace-version-test-") as directory:
            env_file = Path(directory) / "github-env"
            env = dict(os.environ, GITHUB_ENV=str(env_file),
                       GITHUB_REF_TYPE="tag", GITHUB_REF_NAME="v" + expected)
            result = subprocess.run([sys.executable, str(ROOT / "scripts/release_version.py")],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(env_file.read_text(encoding="utf-8"), f"VERSION={expected}\n")

    def test_project_not_minimum_cmake_version(self):
        text = "cmake_minimum_required(VERSION 3.22)\nproject(ShellSpace VERSION 0.4.1)"
        self.assertEqual(version_module.read_version(text, "v0.4.1"), "0.4.1")

    def test_bad_or_missing_version(self):
        for text in ("", "project(ShellSpace VERSION unknown)"):
            with self.assertRaises(ValueError):
                version_module.read_version(text)

    def test_wrong_tag(self):
        with self.assertRaises(ValueError):
            version_module.read_version("project(ShellSpace VERSION 0.4.1)", "v0.4.0")


class PrepareIRTests(unittest.TestCase):
    def test_missing_assets_generate_then_verify(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(prepare_ir.subprocess, "run") as run:
            prepare_ir.prepare(directory)
            self.assertEqual(run.call_count, 2)
            self.assertTrue(run.call_args_list[0].args[0][1].endswith("make_ir.py"))
            self.assertTrue(run.call_args_list[1].args[0][1].endswith("verify_ir.py"))
            self.assertTrue(all(c.kwargs["check"] for c in run.call_args_list))

    def test_existing_assets_only_verify(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(prepare_ir.subprocess, "run") as run:
            for name in EXPECTED:
                (Path(directory) / name).touch()
            prepare_ir.prepare(directory)
            self.assertEqual(run.call_count, 1)
            self.assertTrue(run.call_args.args[0][1].endswith("verify_ir.py"))

    def test_legacy_files_are_preserved_and_stop_build(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(prepare_ir.subprocess, "run") as run:
            legacy = Path(directory) / "Shell_Kick_body.wav"
            legacy.write_bytes(b"preserve me")
            with self.assertRaises(ValueError):
                prepare_ir.prepare(directory)
            self.assertEqual(legacy.read_bytes(), b"preserve me")
            run.assert_not_called()

class IRGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.generated = tempfile.TemporaryDirectory(prefix="shellspace-generated-")
        cls.addClassCleanup(cls.generated.cleanup)
        subprocess.run([sys.executable, str(ROOT / "IR/make_ir.py"), cls.generated.name],
                       check=True, capture_output=True)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="shellspace-ir-test-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        for path in Path(self.generated.name).glob("*.wav"):
            shutil.copy2(path, self.directory / path.name)
        self.target = self.directory / "Shell_Kick_Birch_Studio.wav"

    def verify(self, expected):
        result = subprocess.run(
            [sys.executable, str(ROOT / "IR/verify_ir.py"), str(self.directory)],
            capture_output=True, encoding="utf-8")
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def replace_wav(self, rate=48000, channels=2, width=3, silent=False):
        with wave.open(str(self.target), "wb") as wav:
            wav.setparams((channels, width, rate, 0, "NONE", "not compressed"))
            sample = bytes(width) if silent else (100).to_bytes(width, "little")
            wav.writeframes(sample * channels * 64)

    def test_valid_bundle(self):
        self.verify(0)

    def test_missing_file(self):
        self.target.unlink()
        self.verify(1)

    def test_silent_file(self):
        self.replace_wav(silent=True)
        self.verify(1)

    def test_wrong_rate(self):
        self.replace_wav(rate=44100)
        self.verify(1)

    def test_wrong_channels(self):
        self.replace_wav(channels=1)
        self.verify(1)

    def test_wrong_bit_depth(self):
        self.replace_wav(width=2)
        self.verify(1)

    def test_corrupt_file(self):
        self.target.write_bytes(b"not a WAV")
        self.verify(1)

    def test_truncated_file(self):
        self.target.write_bytes(self.target.read_bytes()[:-6])
        self.verify(1)

    def test_unexpected_legacy_file(self):
        shutil.copy2(self.target, self.directory / "Shell_Kick_body.wav")
        self.verify(1)

    def test_missing_directory(self):
        self.directory = self.directory / "missing"
        self.verify(1)


if __name__ == "__main__":
    unittest.main()
