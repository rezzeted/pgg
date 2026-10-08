#!/usr/bin/env python3
"""Cross-platform layout generation benchmark with a regression gate.

Runs the `benchmark_layout` binary for a list of maps and fails (exit 1) when a median
generation time exceeds its threshold. Replaces tools/benchmark_layout_generation.ps1
(which is Windows-only).

usage: python3 tools/benchmark_layout_generation.py [--bin PATH] [--check]

  --bin PATH   path to benchmark_layout (default: _build-release/bin/benchmark_layout,
               falling back to _build/bin/benchmark_layout)
  --check      enforce thresholds (exit 1 on regression); without it just prints timings
"""

import argparse
import os
import re
import subprocess
import sys

# (map file, iterations, median threshold in ms) — thresholds are generous regression gates,
# not targets; measured on Apple Silicon Release after stages H–H4.
BENCHMARKS = [
    ("tutorial_basic.yml", 10, 2000.0),
    ("9vertices.yml", 5, 5000.0),
    ("tutorial_corridors.yml", 3, 10000.0),
    ("dragonAge.yml", 3, 10000.0),
    ("17vertices.yml", 3, 30000.0),
    ("41vertices.yml", 2, 60000.0),
]


def find_binary(explicit: str | None) -> str:
    if explicit:
        return explicit
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for candidate in ("_build-release/bin/benchmark_layout", "_build/bin/benchmark_layout"):
        path = os.path.join(root, candidate)
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path
    sys.exit("benchmark_layout not found; build the project first (release-macos preferred)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default=None)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    binary = find_binary(args.bin)
    failures = 0
    for map_name, iterations, threshold in BENCHMARKS:
        cmd = [binary, "--map", map_name, "--iterations", str(iterations)]
        if args.check:
            cmd += ["--threshold-ms", str(threshold)]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        line = proc.stdout.strip()
        print(f"{line}  (threshold {threshold} ms)" if args.check else line)
        if proc.returncode != 0:
            failures += 1
            if proc.stderr:
                print(proc.stderr.strip(), file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
