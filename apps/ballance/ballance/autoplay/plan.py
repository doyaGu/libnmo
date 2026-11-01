"""A plan for playing a level, in JSON, and its points for the autopilot.

    {"start": "PR_Resetpoint_01",
     "legs": [{"to": NAME or [x, y, z], "floors": [...], "avoid": [...]},
              {"steer": "X,Z[,Y[,F]];...", "y": Y}, ...],
     "fixes": [{"box": [x0, x1, z0, z1], "points": "X,Z...;...", "run": 0}, ...]}

The ball starts at a marker and goes through the legs in turn.  A leg with
"to" is searched for, from where the last ended to a marker or element (by
name) or a point; the ground is the level's, with "floors" ([x0, x1, z0,
z1, height], solid) as a mechanism has left them by then, and without the
ground within 5 (or r) of each "avoid" (a name, or [x, z] or [x, z, r]): a
trafo not to touch, where a pushed box ends up.  A leg with "steer" is given
(a push, a hold), and ends at its last point, at height "y".  Each leg may
say why in a "note".

"fixes" then replace a run of the points in a box with points by hand,
where the autopilot can't follow the path found closely enough.
"""

from __future__ import annotations

import json
import sys
from collections.abc import Callable

from ..view.scene import SceneObject
from .search import clearance, find_path, nearest
from .steer import SteerPoint, fix, parse, simplify
from .terrain import Point3, Terrain, terrain

AVOID_REACH = 5.0


def play(scene: list[SceneObject], plan: dict,
         log: Callable[[str], None] = lambda line: print(line, file=sys.stderr)
         ) -> list[SteerPoint]:
    """The plan's points for the autopilot, the legs logged as they are
    found; ValueError if a leg has no path."""
    grounds: dict[str, tuple[Terrain, dict]] = {}
    points: list[SteerPoint] = []
    at: str | Point3 = plan["start"]
    for leg in plan["legs"]:
        if "steer" in leg:
            given = parse(leg["steer"])
            points += given
            at = (given[-1].x, leg.get("y", 0.0), given[-1].z)
            continue
        key = json.dumps([leg.get("floors", []), leg.get("avoid", [])])
        if key not in grounds:
            grounds[key] = _ground(scene, leg)
        ground, clear = grounds[key]
        start = nearest(ground, _where(ground, at))
        goal = nearest(ground, _where(ground, leg["to"]))
        path = find_path(ground, clear, start, goal) if start and goal else None
        log(f"{at} -> {leg['to']}: {len(path) if path else 'no'} cells")
        if path is None:
            raise ValueError(f"no path from {at} to {leg['to']}")
        found = simplify(path)
        points += found if not points else found[1:]
        at = leg["to"]
    for f in plan.get("fixes", []):
        points = fix(points, tuple(f["box"]), parse(f["points"]), f.get("run", 0))
    return points


def _ground(scene: list[SceneObject], leg: dict) -> tuple[Terrain, dict]:
    ground = terrain(scene, [tuple(f) for f in leg.get("floors", [])])
    for avoid in leg.get("avoid", []):
        if isinstance(avoid, str):
            x, _, z = ground.marks[avoid]
            ground.without(x, z, AVOID_REACH)
        else:
            ground.without(avoid[0], avoid[1], avoid[2] if len(avoid) > 2 else AVOID_REACH)
    return ground, clearance(ground)


def _where(ground: Terrain, place: str | list | tuple) -> Point3:
    return ground.marks[place] if isinstance(place, str) else tuple(place)
