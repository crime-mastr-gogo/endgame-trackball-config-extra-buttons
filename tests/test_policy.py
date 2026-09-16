import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class Policy(unittest.TestCase):
    def test_compiled_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / "policy"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-I", str(ROOT / "include"),
                            str(ROOT / "tests/test_policy.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
