#!/usr/bin/env python3
"""Check a level: its floors' geometry, and (for a built one) a run of the
game along the builder's points.

    ballance_verify_level.py SOURCE [--play FRAMES --game DIR]

SOURCE is a folder a level was built in (manifest.json and its OBJ meshes,
as ballance_build_level.py writes them) or a level file (Level_NN.NMO, read
through the nmo CLI).  The floors are checked for walls left open at their
ends, holes in their tops and tops of two objects lying on one another
(ballance.verify.geometry); some are meant (the cradle's pocket is a hole),
and the originals have some too (Level_01: 21 open wall ends, and that
pocket).

With --play, the built level is patched into Level_01.NMO in its folder and
played for FRAMES in the game installed in DIR (`nmo behavior run`, physics
by ivp), the ball steered along route_points.json; the checkpoints, the
finish and any fall are reported, and where the ball stood still a while
(the ball changers hold it 126 frames; a point that waits says what for).
The exit status is 1 if the level does not finish or the ball falls.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument  # noqa: E402
from ballance.verify.geometry import check  # noqa: E402
from ballance.verify.play import play  # noqa: E402
from ballance.view import scene  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description="Check a level's floors and play it through.")
    parser.add_argument("source", help="A built level's folder, or Level_NN.NMO")
    parser.add_argument("--play", type=int, metavar="FRAMES",
                        help="Play the built level for FRAMES and report how it went")
    parser.add_argument("--game", help="Ballance installation folder (holds base.cmo)")
    add_nmo_argument(parser)
    args = parser.parse_args()

    source, nmo = Path(args.source), Nmo(args.nmo)
    findings = check(scene.load(source, nmo, hidden=True))
    for finding in findings:
        print(finding)
    print(f"{len(findings)} findings in the floors")
    if args.play is None:
        return 0
    if not source.is_dir() or not args.game:
        parser.error("--play needs a built level's folder and --game")
    result = play(source, args.play, nmo, Path(args.game))
    for frame, what in result.events:
        print(f"[{frame}] {what}")
    for stall in result.stalls:
        print(stall)
    if result.finished is None:
        print(f"the level did not finish in {args.play} frames")
    return 0 if result.finished is not None and not result.falls else 1


if __name__ == "__main__":
    sys.exit(main())
