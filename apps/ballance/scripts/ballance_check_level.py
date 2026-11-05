#!/usr/bin/env python3
"""Check a Ballance level file against the rules of docs/ballance-level-format.md.

Reads the level with the nmo CLI and reports, by severity:

  error    the level does not play: no start or end, reset points and
           checkpoints that do not add up, no floors, no fall zone group
  warning  it plays but something is off: falls never detected, an element
           in no sector, a visible fall zone, a start or checkpoint without
           its geometry, no sky
  info     what the game reports and gets over, such as a missing element
           group (one load-time error, like the original levels)

Exits with 1 when there are errors, 0 otherwise.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument  # noqa: E402
from ballance.extract.check import check  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Check a Ballance level file against the level format rules.'
    )
    parser.add_argument('level', help='Level file (Level_NN.NMO)')
    add_nmo_argument(parser)
    parser.add_argument('--json', action='store_true', help='Print the findings as JSON')
    args = parser.parse_args()

    try:
        report = check(Path(args.level), Nmo(args.nmo))
    except (RuntimeError, OSError, KeyError, ValueError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps({'level': args.level, 'findings': report.items}, indent=2))
    else:
        for item in report.items:
            print(f'{item["severity"]:7s} {item["code"]}: {item["message"]}')
        print(
            f'{report.count("error")} errors, {report.count("warning")} warnings, '
            f'{report.count("info")} notes'
        )
    return 1 if report.count('error') else 0


if __name__ == '__main__':
    sys.exit(main())
