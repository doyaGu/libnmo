#!/usr/bin/env python3
"""Verify a built project or original level and save a structured report.

    ballance_verify_level.py SOURCE [--play FRAMES --game DIR --sector N]
        [--report FILE] [--json] [--strict]

A built project is checked against its recorded input hashes, its floor
geometry and the final scenery placements around rails. --play also runs
the real IVP gameplay harness and reports checkpoints, falls, reward
proximity, stalls and the trace's clearance from death zones. Older builds
without route.json/build_info.json still work, with unavailable checks
marked explicitly; rebuild them to enable all static checks.

Built projects save validation.json by default; --report chooses another
path (also for original NMO files). --json prints the same report. Geometry
findings need review by default, since original modules contain intentional
openings; --strict makes them failures. Exit 1 means a failed check, 2 an
invocation/input error. A review result keeps its findings and exits 0.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument  # noqa: E402
from ballance.verify.report import summary, verify, write  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description='Check geometry, scenery clearance and gameplay.')
    parser.add_argument('source', type=Path, help="A built level's folder, or Level_NN.NMO")
    parser.add_argument('--play', type=int, metavar='FRAMES', help='Run the autopilot for FRAMES')
    parser.add_argument(
        '--game',
        type=Path,
        default=os.environ.get('BALLANCE'),
        help='Ballance installation folder (default: $BALLANCE)',
    )
    parser.add_argument('--sector', type=int, default=1, help='Play from sector N (default: 1)')
    parser.add_argument(
        '--report', type=Path, help='JSON file (default: SOURCE/validation.json for projects)'
    )
    parser.add_argument('--json', action='store_true', help='Print the structured report')
    parser.add_argument('--strict', action='store_true', help='Fail on floor geometry findings too')
    add_nmo_argument(parser)
    args = parser.parse_args()
    output = args.report or (args.source / 'validation.json' if args.source.is_dir() else None)
    try:
        report = verify(
            args.source,
            Nmo(args.nmo),
            frames=args.play,
            game=args.game,
            sector=args.sector,
            strict=args.strict,
        )
        if output:
            write(report, output)
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        # Replace a previous successful report even when inputs cannot be
        # read. An old success must not survive a failed verification.
        report = {
            'schema_version': 1,
            'source': str(args.source.resolve()),
            'status': 'failed',
            'checks': {'input': {'status': 'failed', 'error': str(error)}},
        }
        if output and output.parent.is_dir():
            try:
                write(report, output)
            except OSError:
                pass  # The original error below still explains the failure.
        if args.json:
            print(json.dumps(report, indent=2, allow_nan=False))
        else:
            print(f'error: {error}', file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, indent=2, allow_nan=False))
    else:
        print('\n'.join(summary(report)))
        if output:
            print(f'report: {output}')
    return 1 if report['status'] == 'failed' else 0


if __name__ == '__main__':
    sys.exit(main())
