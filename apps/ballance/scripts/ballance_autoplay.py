#!/usr/bin/env python3
"""Find the points for `nmo behavior run --steer` that play a level through.

    ballance_autoplay.py LEVEL [PLAN.json] [-o route.txt]

LEVEL is a level file (Level_NN.NMO, read through the nmo CLI) or a folder a
level was built in.  PLAN names what the ball does in turn: the legs to
search for and those given, and fixes by hand; its format is described in
ballance/autoplay/plan.py.  Without one, the ball goes from the first reset
point to each checkpoint in turn and then the balloon, which checks a level
whose mechanisms need nothing done to them: a path from each to the next.
The points are written as --steer takes them, without the "@FRAME"; each
leg's path is logged as it is found.

Keep authored plans and generated routes in a local, ignored directory.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.autoplay.plan import default_plan, play  # noqa: E402
from ballance.autoplay.steer import format_points  # noqa: E402
from ballance.core.nmo import Nmo, add_nmo_argument  # noqa: E402
from ballance.view import scene as scenes  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description='Find the steer points that play a level.')
    parser.add_argument('level', help='Level_NN.NMO, or a folder with manifest.json')
    parser.add_argument(
        'plan', nargs='?', help='The plan, in JSON (default: checkpoints, then the balloon)'
    )
    parser.add_argument('-o', '--output', help='Where to write the points (default: print)')
    add_nmo_argument(parser)
    args = parser.parse_args()

    scene = scenes.load(Path(args.level), Nmo(args.nmo))
    plan = (
        json.loads(Path(args.plan).read_text(encoding='utf-8'))
        if args.plan
        else default_plan(scene)
    )
    try:
        points = play(scene, plan)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    text = format_points(points) + '\n'
    if args.output:
        Path(args.output).write_text(text, encoding='utf-8')
    else:
        sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
