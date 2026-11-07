#!/usr/bin/env python3
"""Measure fixed-input gameplay tolerance on an explicit perturbation grid."""

import argparse
import os
import subprocess
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument
from ballance.verify.tolerance import run_tolerance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('suite', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--game', type=Path, default=os.environ.get('BALLANCE'))
    parser.add_argument('--experiment', help='Measure only this experiment ID')
    add_nmo_argument(parser)
    args = parser.parse_args()
    if not args.game:
        parser.error('--game or BALLANCE is required')
    try:
        result = run_tolerance(
            args.source, args.suite, args.out, Nmo(args.nmo), args.game, args.experiment
        )
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 2
    for case in result['experiments']:
        print(
            f'{case["id"]}: {case["status"]} '
            f'({case["successful_samples"]}/{case["sample_count"]} samples)'
        )
    print(f'report: {args.out / "tolerance.json"}')
    return 0 if result['status'] != 'failed' else 1


if __name__ == '__main__':
    sys.exit(main())
