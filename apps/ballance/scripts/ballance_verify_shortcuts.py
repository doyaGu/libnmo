#!/usr/bin/env python3
"""Verify same-sector speedrun cuts and compare native full-level completion times."""

import argparse
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument
from ballance.verify.shortcuts import run_shortcuts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('shortcuts', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--game', type=Path, default=os.environ.get('BALLANCE'))
    add_nmo_argument(parser)
    args = parser.parse_args()
    if not args.game:
        parser.error('--game or BALLANCE is required')
    try:
        result = run_shortcuts(args.source, args.shortcuts, args.out, Nmo(args.nmo), args.game)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 2
    print(f'report: {args.out / "shortcuts.json"}')
    return 0 if result['status'] == 'passed' else 1


if __name__ == '__main__':
    sys.exit(main())
