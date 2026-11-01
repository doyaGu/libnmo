#!/usr/bin/env python3
"""Build a Ballance level from a route, as a libnmo project manifest.

A route is the track as the ball rolls it, sector by sector, in JSON:

    {
      "start": {"x": 0, "z": 0, "heading": "-x"},
      "sectors": [
        [{"road": 10},
         {"area": [25, 25], "exit": "left",
          "elements": [{"kind": "P_Extra_Point", "at": [12.5, 0]}]},
         {"road": 30, "width": "wide", "drop": 5},
         {"turn": "right"},
         {"trafo": "stone"},
         {"road": 10}],
        [...]
      ]
    }

The start's marker stands at (x, z), the edges of the road that leaves it at
height 0, and the track leaves toward heading (+x, -x, +z or -z).  Each
sector is a list of pieces, laid end to end on the floors' 2.5 grid:

- {"road": length, "width": "narrow" or "wide", "drop": dy} -- a straight
  road (narrow unless given), coming down dy over its length.
- {"area": [along, across], "exit": "straight", "left" or "right"} -- a
  square or oblong area entered at the middle of one side and left at the
  middle of the opposite side, or of the side to the left or right.  A
  pillar stands under each corner.
- {"turn": "left" or "right"} -- a square corner as wide as the road.
- {"plank": true} -- the arched wood plank over a gap 10 long, as Level_03
  has it, with funnels flattening a narrow road to its ends and back.
- {"walkway": true} -- the wavy wood walkway 70.32 long, as Level_08 has
  it, with funnels flattening a narrow road to its ends and back.
- {"twist": true} -- the wood twist, a chute 58.66 long turning the ball
  once round, as Level_07 has it, its mouths on flat floors that funnels
  lead a narrow road onto and off.
- {"jump": true} -- the wood jump, as Level_09 has it: a funnel flattens a
  narrow road to the top of a wooden slope 56.4 long, which drops the ball
  and throws it 14.29 over a gap into a 15 by 15 area 25.02 lower; a funnel
  turns the area's way out into a narrow road again.  A paper ball falls
  short, in Level_09 too.
- {"trafo": "paper", "stone" or "wood"} -- a ball changer on its pad, on a
  narrow road.  The original levels all turn the ball back to wood before
  the balloon; the builder warns when a route does not.
- {"module": "P_Modul_03"} -- the lift, as Level_07 has it: a narrow road
  runs through a funnel to its gate; the ball pushes its seven walls off,
  rolling out to each, and rides its floor up 17.64 to the platform on the
  far side, whose flat middle a funnel turns back into a narrow road, 56.81
  on from where the first funnel begins.  Only a wood ball gets up, in the
  originals too.
- {"module": "P_Modul_08"} -- the swing, as Level_09 has it: a seat hung
  over a gap 15 long in a flat road 25 long, which funnels lead onto and off
  a narrow road; the ball rides the seat across from the near end of its
  swing.  A stone ball falls off it, in Level_09 too.
- {"module": "P_Modul_19"} -- the flaps, as Level_09 has them: an axle
  across two roads 15 apart, each with a gap the flap over it bridges.  A
  funnel leads the road onto its flat road to the gap, where the flap
  stands up; the other road, to the left, has its flap down with a stone
  ball on it.  The ball pushes the upright flap over and crosses, throwing
  the stone ball off; the road goes on 21.65 on.  A paper ball can't push
  the flap over, in Level_09 too.
- {"module": "P_Modul_25"} -- the drawbridge: an upright plank the ball
  pushes over a gap 10 long, between flat blocks a narrow road runs into
  through funnels, as Level_03's are.  The original levels bring a wood or
  stone ball to it.
- {"module": "P_Modul_26", "count": n} -- n (1 to 4) sacks swinging over a
  flat road at a narrow road's edge height, 15 apart, between funnels, as
  levels 8 to 13 hang them.
- {"module": "P_Modul_01"} -- two pushers, with Level_01's plaza around
  them: a funnel leads the road onto a corridor 52.5 long, which narrows
  back into the road at its end.  A frame stands across the corridor at each pusher, 28.15
  and 38.15 on; the ball drops off the corridor to the low ground beside it,
  pushes each frame's target until its crossbar has moved aside, comes back
  up the ramp of the road round the plaza and goes on along the corridor's
  right side, past the crossbars.
- {"module": "P_Modul_17"} -- a rotating arm and the drawbridge it tips, as
  Level_03 has them: the narrow road runs 33.33 to and across a gap whose
  plank stands on the far side, and a loop of road beside it climbs to an
  upper road the arm reaches over.  The ball pushes the arm round on the
  upper road, the arm's other end knocks the plank across the gap, and the
  ball comes back down and crosses.  A paper ball can't turn the arm.
- {"module": "P_Modul_29"} -- nine plates hung on hinges like a rope
  bridge over a gap 15.33 long, as Level_03 has them: the narrow road's V
  runs to the gap and on from its far side.  A stone ball falls through
  them.
- {"module": "P_Modul_30"} -- a seesaw over a gap 14.8 long, its plank
  resting on a stopper lip at either end, as Level_03 has it; the road runs
  to and from it as to the plates.
- {"module": "P_Modul_34"} -- the stone on a box, with Level_01's plaza
  around it: a funnel leads the road onto a walkway 5 wide over a pit 45
  wide, with a stone trafo on its first 5; the stone stands in a gap in the
  walkway 24.6 on, on a box in a channel under it.  The ball, made stone,
  goes down into the pit, pushes the box out along the channel so that the
  stone drops flush into the gap, comes back up and crosses.  The track
  leaves the plaza 55 on, 2.1 higher, on a wide road, which the route goes
  on with (Level_01's climbs on 1.3 every 5).
- {"module": "P_Modul_37", "side": "left" or "right"} -- the switch, as
  Level_05 has it: the track crosses a gap between two blocks, and a stone
  ball rolled down a spur onto the target beside it tips a plank across the
  gap.  The piece is a square area the track enters and leaves straight on
  toward the gap, and a spur from its side that turns twice to end at the
  target; the ball goes down the spur, pushes the target until the plank
  falls, comes back and goes on.  A wood ball does not tip it.
- {"module": "P_Modul_41", "count": n} -- a row of n (2 to 8) slabs on axles
  across the track, between funnels, as levels 7 to 11 lay them.  Only a
  paper ball crosses them: a wood or stone ball tips them and falls.
- {"rail": length, "drop": dy} -- a straight rail, coming down dy.
- {"rail_turn": "left" or "right", "drop": dy} -- the originals' rail
  bend, with a straight 1.9 before and after it so that it ends 10 along and
  10 aside, coming down dy evenly along it (Level_01's spiral comes down 1.26
  a bend).
- {"fall": dy} -- the rail ends in the air and the ball falls dy (at most
  15) into the area that comes next, which starts 5 back under the rail's
  end, its rim closed (as Level_01's rails drop the ball into its pits).
  With "split": true, the rail's tubes part at its end instead, the ball
  dropping between them, as Level_01's A01_Rail_02_01 do.
- {"log": length} -- a half log (Level_12's "halfpipes"), a whole number of
  50-long segments, that a rail's fall drops the ball onto 5 past the
  rail's end; {"log": length, "turn": "left" or "right", "drop": dy} -- one
  crossing dy below the last log, 25 from its far end, that the ball rolls
  off the last onto, as Level_12's logs do.  A fall from a log's end drops
  the ball into an area.
- {"cradle": true} -- the rail bends straight down into a cradle on a 15 by
  15 block, as Level_01's A01_Rail_09 does, 9.09 below the rail; the track
  leaves the block to the right, from a narrow road's end on it, 7.5 from
  its middle.  A stone ball can't climb out of the pocket.
  Rails run from a narrow road, a wide one or an area to a narrow road, a
  wide one, an area, a fall or a cradle (ballance.kit.rails); a wide road
  meets a rail through a piece 5 long whose middle narrows to a V, as
  Level_01's A01_Floor_03 meets A01_Rail_07.

An area may hold a fan, {"kind": "P_Modul_18", "at": [along, right]} among
its elements, on the corner of four of its flat inside cells, and then
"rise": dy: the fan lifts a paper ball onto the next piece, dy higher (at
most 16), which starts at the area's exit as Level_03's do.

Every piece may carry "elements": [{"kind": "P_Box", "at": [along, right]}],
placed from where the piece starts, along the track and to its right, and
resting on the floor there as the originals' do (ELEMENT_LIFT in
ballance.game.measures).  Each
sector but the last ends in a checkpoint and the last in the balloon; both
are reached from a narrow road, and every sector starts with one.

The level is built as the original levels are (docs/ballance-level-format.md):
the floors with ballance.kit, the start, checkpoints and balloon turned
with the track, their lantern towers and the balloon's striped poles beside
it, the fall zone hidden under all of it.  With --elements, it uses the
meshes apps/ballance/scripts/ballance_level_elements.py exported from a Ballance
installation; without, boxes stand in for the markers, elements and the
start's and checkpoints' floors, and there is no scenery and no sky.

The output directory gets manifest.json, the OBJ meshes it names, and
route_points.json, the points along the middle of the track from the start to
the balloon, for driving a ball along it; a point over a fan has a third
value, the height to wait there for, one before a swing four more, the
seat to wait for and where, within a reach, and one over a jump's gap a
fourth, "rush", with no height (null): run at flat out to it (`nmo
behavior run --steer`).  Turn it into a level file with:

    nmo patch apply --project <out>/manifest.json -o <out>/Level_01.NMO
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.route.build import build  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build a Ballance level from a route, as a project manifest.")
    parser.add_argument("route", help="The route, as JSON")
    parser.add_argument("out", help="Directory for manifest.json and the OBJ meshes")
    parser.add_argument("--elements",
                        help="Folder of <Kind>.obj meshes from ballance_level_elements.py")
    parser.add_argument("--name", default="Level", help="The document's name")
    args = parser.parse_args()
    route = json.loads(Path(args.route).read_text(encoding="utf-8"))
    try:
        return build(route, Path(args.out), Path(args.elements) if args.elements else None,
                     args.name)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
