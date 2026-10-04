#!/usr/bin/env python3
"""Run every host test suite. The real sources are built for the host with the
PS4 calls stubbed, so this needs no console.

    docker run --rm --network none -v "$PWD:/src" -w /src \
        control4free-launcher-build python3 -B tests/run.py
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUITES = ['test_logging', 'test_frontend', 'test_web', 'test_recovery', 'test_launcher']


def main():
    failed = []
    for name in SUITES:
        print(f'=== {name} ===', flush=True)
        result = subprocess.run([sys.executable, '-B', f'tests/{name}.py'], cwd=ROOT)
        if result.returncode != 0:
            failed.append(name)
    if failed:
        print('FAILED: ' + ', '.join(failed))
        return 1
    print(f'All {len(SUITES)} suites passed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
