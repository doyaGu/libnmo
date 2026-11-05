#!/usr/bin/env python3
"""Replay an authored gameplay scenario suite against a current built level."""

import argparse
import os
import subprocess
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument
from ballance.verify.scenarios import run_suite


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('suite', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--game', type=Path, default=os.environ.get('BALLANCE'))
    parser.add_argument('--scenario', help='Replay only this scenario ID')
    add_nmo_argument(parser)
    args = parser.parse_args()
    if not args.game:
        parser.error('--game or BALLANCE is required')
    try:
        result = run_suite(
            args.source, args.suite, args.out, Nmo(args.nmo), args.game, args.scenario
        )
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 2
    for case in result['scenarios']:
        print(f'{case["id"]}: {case["status"]} (expected {case["expected"]})')
    print(f'report: {args.out / "scenarios.json"}')
    return 0 if result['status'] == 'passed' else 1


if __name__ == '__main__':
    sys.exit(main())
