#!/usr/bin/env python3
"""Run one POST_BUILD check and re-emit its failure as an annotation.

The raw job logs are not reachable from automation (the log endpoints
redirect to a host the sandbox cannot open), and a plain python exit code
does not produce any check annotation. This wrapper keeps the exit code
untouched but, on failure, prints a line shaped like a compiler error
("file(1): error C999: ...") so the runner's C++ problem matcher turns the
failure into a check annotation that names the check that failed.

Uso:
    python3 run-check.py check-exports.py dist/Win7TaskbarCore.dll
"""

import subprocess
import sys
import os

if len(sys.argv) != 3:
    raise SystemExit("uso: run-check.py <check.py> <dll>")

check_name = os.path.basename(sys.argv[1]).replace("\\", "/")
# MSBuild runs POST_BUILD commands with the build folder as the working
# directory, so a bare relative script name would not resolve. Anchor it to
# this wrapper's own folder instead.
check_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), check_name)
proc = subprocess.run([sys.executable, check_path, sys.argv[2]])
if proc.returncode != 0:
    print(f"native/tools/{check_name}(1): error C999: "
          f"{check_name} failed (exit {proc.returncode})")
sys.exit(proc.returncode)
