#!/usr/bin/env python3
"""Run firmware logic with fake hardware; requires Python 3 and a C++17 compiler."""

import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
host = root / "tests" / "host"
with tempfile.TemporaryDirectory(prefix="spoolbuddy-tests-") as temporary:
    executable = Path(temporary) / "regressions"
    subprocess.run(
        [
            os.environ.get("CXX", "c++"),
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-I", str(host / "fakes"),
            "-I", str(root / "src"),
            str(host / "regressions.cpp"),
            str(root / "src" / "scale.cpp"),
            str(root / "src" / "touch_display.cpp"),
            "-o", str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
