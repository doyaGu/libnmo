"""The pieces of a route, each laid on a Layout from where the track is.

Each lay_<kind> function lays one piece from the Track, moves the Track to
where the piece ends, and returns the floor's height along it, (height at
its start, at its end, its length), for the elements placed on it; a piece
with no floor to stand on returns None.  lay_piece() picks the function,
docks rails to the floors they start and end at, and places the elements.
"""

from __future__ import annotations

import math
from typing import TYPE_CHECKING, Callable

from ..core.geometry import across, at, axis_of, back_of, left_of, rect, side_of, turn_of
from ..game.measures import (ARMS_ABOVE, ARMS_BRIDGE, ARMS_LENGTH, ARMS_MODULE, ARMS_PATH, BRIDGES,
                             ELEMENT_LIFT, FAN_BELOW, FAN_REACH, FLAPS_COUNTERWEIGHT, FLAPS_LENGTH,
                             FLAPS_MODULE, FUNNEL, LIFT_BELOW, LIFT_ENTRY, LIFT_EXIT, LIFT_RIDE,
                             LIFT_TOP, LIFT_WALLS, PILLAR_INSET, PLANK_BELOW, PLANK_LENGTH,
                             PUSHERS_ASIDE, PUSHERS_LENGTH, PUSHERS_MODULES, PUSHERS_PATH,
                             RAIL_FALL, RAIL_OVERHANG, SACK_ABOVE, STONEBOX_LENGTH, STONEBOX_MODULE,
                             STONEBOX_PATH, STONEBOX_RISE, STONEBOX_TRAFO, SWING_ABOVE, SWING_FAR,
                             SWING_GO, SWING_HALF, SWING_SEAT, SWING_WAIT, SWITCH_AREA,
                             SWITCH_CROSS, SWITCH_HOLD, SWITCH_MODULE, SWITCH_SPUR, SWITCH_TARGET,
                             TRAFO_LIFT)
from ..kit.floors import CELL, NARROW, RISE, WALL
from ..kit.pieces import channel, funnel, pad, strip
from ..kit.rails import ABOVE_V, BEND_RADIUS, Ring, bend
from .modules import MODULE_FLOORS, MODULES, NEEDS_BALL, module_layout

if TYPE_CHECKING:
    from .layout import Layout, Sector, Track

Ramp = tuple[float, float, float]  # height at the start, at the end, length

WIDTHS = {"narrow": NARROW, "wide": 2 * NARROW}
# A rail bend, with its straights, ends 10 along and 10 aside: on the grid.
BEND_SPAN = 4 * CELL


def lay_road(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"road": length, "width": "narrow" or "wide", "drop": dy}"""
    length = float(piece["road"])
    width = WIDTHS[piece.get("width", "narrow")]
    end = track.h - float(piece.get("drop", 0.0))
    layout.floors.road(*rect(track.p, track.d, length, width), track.h, end,
                       along=axis_of(track.d))
    ramp = (track.h, end, length)
    track.p, track.h, track.width = at(track.p, track.d, length), end, width
    layout.points.append(list(track.p))
    track.floor = "road" if width == NARROW else "wide"
    return ramp


def lay_rail(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> None:
    """{"rail": length, "drop": dy}"""
    track.p = at(track.p, track.d, float(piece["rail"]))
    track.h -= float(piece.get("drop", 0.0))
    track.run.append(Ring(track.p[0], track.h + ABOVE_V, track.p[1], track.d[0], track.d[1]))
    layout.points.append(list(track.p))
    track.floor = "rail"


def lay_rail_turn(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> None:
    """{"rail_turn": "left" or "right", "drop": dy}: the bend, coming down dy
    evenly along it and its straights."""
    sign = 1 if piece["rail_turn"] == "right" else -1
    side = side_of(track.d, piece["rail_turn"])
    drop = float(piece.get("drop", 0.0))
    straight = BEND_SPAN - BEND_RADIUS
    arc = BEND_RADIUS * math.pi / 2
    per = drop / (2 * straight + arc)  # a unit along it
    a = at(track.p, track.d, straight)
    rings = bend(a[0], track.h + ABOVE_V - per * straight, a[1], track.d, side, per * arc)
    track.p = at(track.p, track.d, BEND_SPAN, sign * BEND_SPAN)
    track.d = side
    track.h -= drop
    track.run += rings + [Ring(track.p[0], track.h + ABOVE_V, track.p[1], side[0], side[1])]
    layout.points += [[r.x, r.z] for r in rings[3::3]] + [list(track.p)]
    track.floor = "rail"


def lay_fall(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> None:
    """{"fall": dy}: the rail ends in the air, and the ball falls dy onto the
    area that comes next, which starts RAIL_OVERHANG back under the rail's
    end (as Level_01's rails drop the ball into its pits)."""
    fall = float(piece["fall"])
    if not 0 < fall <= RAIL_FALL:
        raise ValueError(f"a rail drops the ball at most {RAIL_FALL}")
    part.rails.runs.append(track.run)
    track.run = None
    track.h -= fall
    track.p = at(track.p, track.d, -RAIL_OVERHANG)
    track.floor = "fall"


def lay_area(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"area": [along, across], "exit": "straight", "left" or "right",
    "rise": dy}; with a rise, a fan among its elements lifts the ball onto the
    next piece, dy higher, on a grid of its own (as Level_03's do).  After a
    rail's fall it starts on a grid of its own, its rim closed."""
    along, across = (float(v) for v in piece["area"])
    p, d, h = track.p, track.d, track.h
    if track.floor == "fall":
        layout.floors = part.grid(p)
    layout.floors.area(*rect(p, d, along, across), h)
    for a in (PILLAR_INSET, along - PILLAR_INSET):
        for c in (-across / 2 + PILLAR_INSET, across / 2 - PILLAR_INSET):
            x, z = at(p, d, a, c)
            layout.pillars.append([x, h + RISE - WALL, z])
    exit = piece.get("exit", "straight")
    middle = at(p, d, along / 2)
    if exit == "straight":
        track.p = at(p, d, along)
    else:
        track.d = side_of(d, exit)
        track.p = at(middle, track.d, across / 2)
    rise = float(piece.get("rise", 0.0))
    fans = [e for e in piece.get("elements", []) if e["kind"] == "P_Modul_18"]
    if rise:
        if not fans:
            raise ValueError("an area rises only with a fan (P_Modul_18) in it")
        if not 0 < rise <= FAN_REACH:
            raise ValueError(f"a fan lifts the ball at most {FAN_REACH}")
        if layout.last_ball != "Paper":
            layout.warn("only a paper ball rides a fan's wind; the ball reaches it as "
                        f"{layout.last_ball.lower()}")
        fan = at(p, d, *(float(v) for v in fans[0]["at"]))
        # wait over the fan until the ball is above the floors ahead
        layout.points.append([fan[0], fan[1], h + rise + RISE + 1.0])
        track.h += rise
        layout.floors = part.grid(track.p)
    else:
        layout.points.append(list(middle))
    layout.points.append(list(track.p))
    track.floor = "area"
    return h, h, along


def lay_turn(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"turn": "left" or "right"}: a square corner as wide as the road."""
    width = track.width
    layout.floors.road(*rect(track.p, track.d, width, width), track.h, track.h,
                       along=axis_of(track.d))
    corner = at(track.p, track.d, width / 2)
    layout.points.append(list(corner))
    track.d = side_of(track.d, piece["turn"])
    track.p = at(corner, track.d, width / 2)
    layout.points.append(list(track.p))
    track.floor = "turn"
    return track.h, track.h, width


def lay_plank(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"plank": true}: the arched wood plank over a gap, as Level_03 has it
    (see PLANK_* in ballance.game.measures), between funnels that flatten a
    narrow road to it and back."""
    if track.width != NARROW:
        raise ValueError("the wood plank lies on a narrow road")
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    layout.open(layout.floors, p, d)
    part.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge, None))
    middle = at(p, d, FUNNEL + PLANK_LENGTH / 2)
    layout.module_floors.append(("Modul27", sector, [middle[0], edge + PLANK_BELOW, middle[1]],
                                 turn_of(d)))
    length = FUNNEL + PLANK_LENGTH
    part.extras.append((funnel, at(p, d, length), d, edge, None))
    track.p = at(p, d, length + FUNNEL)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "road"
    return h, h, length + FUNNEL


def lay_trafo(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"trafo": "paper", "stone" or "wood"}: a ball changer on its pad,
    between funnels."""
    if track.width != NARROW:
        raise ValueError("a ball changer stands on a narrow road")
    kind = piece["trafo"].capitalize()
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    layout.open(layout.floors, p, d)
    middle = at(p, d, FUNNEL + NARROW / 2)
    part.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge, None))
    part.extras.append((pad, middle, edge, f"Floor_Trafo_{kind}"))
    part.extras.append((funnel, at(p, d, FUNNEL + NARROW), d, edge, None))
    layout.elements.append((f"P_Trafo_{kind}", sector,
                            [middle[0], edge + TRAFO_LIFT, middle[1]]))
    layout.trafos.add(kind)
    layout.last_ball = kind
    track.p = at(p, d, 2 * FUNNEL + NARROW)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(middle))
    layout.points.append(list(track.p))
    track.floor = "trafo"
    return h, h, 2 * FUNNEL + NARROW


def check_ball(layout: Layout, kind: str) -> None:
    """Warns when the ball the track brings cannot get past the module."""
    if kind in NEEDS_BALL:
        balls, text = NEEDS_BALL[kind]
        if layout.last_ball not in balls:
            layout.warn(f"a {layout.last_ball.lower()} ball " + text.format(kind=kind))


def lay_module(layout: Layout, sector: int, part: Sector, piece: dict,
               track: Track) -> Ramp | None:
    """{"module": kind, "count": n}: a module, or a row of them, between
    funnels; the road beyond goes on on a grid of its own."""
    kind = piece["module"]
    if kind not in MODULES:
        raise ValueError(f"unknown module {kind}; known: {', '.join(MODULES)}")
    if kind == "P_Modul_37":
        return lay_switch(layout, sector, part, piece, track)
    if kind == "P_Modul_34":
        return lay_stonebox(layout, sector, part, piece, track)
    if kind == "P_Modul_01":
        return lay_pushers(layout, sector, part, piece, track)
    if kind in BRIDGES:
        return lay_bridge(layout, sector, part, piece, track)
    if kind == "P_Modul_17":
        return lay_arms(layout, sector, part, piece, track)
    if kind == "P_Modul_19":
        return lay_flaps(layout, sector, part, piece, track)
    if kind == "P_Modul_03":
        return lay_lift(layout, sector, part, piece, track)
    if kind == "P_Modul_08":
        return lay_swing(layout, sector, part, piece, track)
    if track.width != NARROW or track.floor != "road":
        raise ValueError("a module stands on a narrow road")
    fewest, most = MODULES[kind]
    count = int(piece.get("count", fewest))
    if not fewest <= count <= most:
        raise ValueError(f"{kind} comes {fewest} to {most} in a row")
    length, offsets, walls = module_layout(kind, count)
    check_ball(layout, kind)
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    back = back_of(d)
    layout.open(layout.floors, p, d)
    part.extras.append((funnel, at(p, d, FUNNEL), back, edge, None, walls))
    part.extras.append((funnel, at(p, d, FUNNEL + length), d, edge, None, walls))
    if kind == "P_Modul_26":
        part.extras.append((strip, at(p, d, FUNNEL), d, length, edge))
    for k, offset in enumerate(offsets):
        where = at(p, d, FUNNEL + offset)
        # the ball rolls along the module's -X; sacks turn every other
        if kind == "P_Modul_26":
            turn = turn_of(back if k % 2 == 0 else d)
            layout.modules.append((kind, sector, [where[0], edge + SACK_ABOVE, where[1]], turn))
        else:
            layout.modules.append((kind, sector, [where[0], edge, where[1]], turn_of(back)))
        if kind in MODULE_FLOORS:
            layout.module_floors.append(layout.modules[-1])
    # no point at the module itself: driving at it, the ball would slow down
    # where it has to push or roll over it
    layout.points.append(list(at(p, d, FUNNEL + length)))
    track.p = at(p, d, 2 * FUNNEL + length)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "module"
    return h, h, 2 * FUNNEL + length


def lay_stonebox(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"module": "P_Modul_34"}: the stone on a box with its plaza, as
    Level_01 has it (see STONEBOX_* in ballance.game.measures), entered from
    a narrow road through a funnel onto the walkway, its stone trafo first;
    the track leaves it on a wide road, climbing, which the route goes on
    with."""
    if track.width != NARROW or track.floor != "road":
        raise ValueError("the stone on a box is entered from a narrow road")
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    start = FUNNEL  # the walkway's end, where the plaza's floor begins
    layout.open(layout.floors, p, d)
    part.extras.append((funnel, at(p, d, start), back_of(d), edge, None))
    module = at(p, d, start + STONEBOX_MODULE)
    layout.modules.append(("P_Modul_34", sector, [module[0], edge, module[1]], turn_of(back_of(d))))
    layout.module_floors.append(layout.modules[-1])
    trafo = at(p, d, start + STONEBOX_TRAFO)
    layout.elements.append(("P_Trafo_Stone", sector, [trafo[0], edge + TRAFO_LIFT, trafo[1]]))
    layout.trafos.add("Stone")
    layout.last_ball = "Stone"
    for along, right, hold in STONEBOX_PATH:
        where = at(p, d, start + along, right)
        layout.points.append([where[0], where[1], h - 20.0, hold] if hold else list(where))
    track.p = at(p, d, start + STONEBOX_LENGTH)
    track.h = edge + STONEBOX_RISE
    track.width = 2 * NARROW
    layout.floors = part.grid(track.p)
    layout.floors.open_edge(*across(track.p, d, 2 * NARROW))
    layout.points.append(list(track.p))
    track.floor = "wide"
    return h, h, start + STONEBOX_LENGTH


def lay_bridge(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> None:
    """{"module": "P_Modul_29" or "P_Modul_30"}: the plates or the seesaw
    over a gap, as Level_03 has them (BRIDGES in ballance.game.measures): the
    narrow road's V runs to the gap's edge and on from its far edge, on a
    grid of its own."""
    if track.width != NARROW or track.floor != "road":
        raise ValueError("a bridge spans a gap between narrow roads")
    if piece.get("elements"):
        raise ValueError("elements stand on floors, not on a bridge")
    kind = piece["module"]
    check_ball(layout, kind)
    gap, along, below = BRIDGES[kind]
    p, d, h = track.p, track.d, track.h
    layout.dock(layout.floors, p, d)
    where = at(p, d, along)
    layout.modules.append((kind, sector, [where[0], h + RISE - below, where[1]], turn_of(d)))
    if kind in MODULE_FLOORS:
        layout.module_floors.append(layout.modules[-1])
    track.p = at(p, d, gap)
    layout.floors = part.grid(track.p)
    layout.dock(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "module"


def lay_arms(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"module": "P_Modul_17"}: a rotating arm and the drawbridge it tips,
    with the loop of road to it, as Level_03 has them (see ARMS_* in
    ballance.game.measures), entered from a narrow road, whose V runs on
    into it, and left the same way, on a grid of its own."""
    if track.width != NARROW or track.floor not in ("road", "trafo"):
        raise ValueError("the rotating arm is entered from a narrow road")
    check_ball(layout, "P_Modul_17")
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    layout.open(layout.floors, p, d)
    # the arm's X runs across the track, to its left; the drawbridge's along it
    along, right = ARMS_MODULE
    arm = at(p, d, along, right)
    layout.modules.append(("P_Modul_17", sector, [arm[0], edge + ARMS_ABOVE, arm[1]],
                           turn_of(left_of(d))))
    layout.module_floors.append(layout.modules[-1])
    bridge = at(p, d, ARMS_BRIDGE)
    layout.modules.append(("P_Modul_25", sector, [bridge[0], edge, bridge[1]], turn_of(d)))
    for along, right, hold in ARMS_PATH:
        where = at(p, d, along, right)
        layout.points.append([where[0], where[1], h - 20.0, hold] if hold else list(where))
    track.p = at(p, d, ARMS_LENGTH)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "module"
    return h, h, ARMS_LENGTH


def lay_swing(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"module": "P_Modul_08"}: the swing over the gap in its flat road, as
    Level_09 has them (see SWING_* in ballance.game.measures), the road
    entered and left through funnels from and onto a narrow road."""
    if track.width != NARROW or track.floor not in ("road", "trafo"):
        raise ValueError("the swing is entered from a narrow road")
    check_ball(layout, "P_Modul_08")
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    layout.open(layout.floors, p, d)
    part.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge, None))
    module = at(p, d, FUNNEL + SWING_HALF)
    layout.modules.append(("P_Modul_08", sector, [module[0], edge - SWING_ABOVE, module[1]],
                           turn_of(d)))
    layout.module_floors.append(layout.modules[-1])
    wait = at(p, d, FUNNEL + SWING_HALF - SWING_WAIT)
    for along, reach in (SWING_FAR, SWING_GO):
        seat = at(p, d, FUNNEL + SWING_HALF + along)
        layout.points.append([wait[0], wait[1], h - 20.0, 0, SWING_SEAT, seat[0], seat[1], reach])
    length = FUNNEL + 2 * SWING_HALF
    part.extras.append((funnel, at(p, d, length), d, edge, None))
    track.p = at(p, d, length + FUNNEL)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "module"
    return h, h, length + FUNNEL


def lay_lift(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"module": "P_Modul_03"}: the lift, with the platform it rises to, as
    Level_07 has them (see LIFT_* in ballance.game.measures), entered from a
    narrow road through a funnel up to its gate, and left from the
    platform's flat middle through a funnel onto a narrow road again, on a
    grid of its own."""
    if track.width != NARROW or track.floor not in ("road", "trafo"):
        raise ValueError("the lift is entered from a narrow road")
    check_ball(layout, "P_Modul_03")
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    layout.open(layout.floors, p, d)
    part.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge, None))
    module = at(p, d, LIFT_ENTRY)
    y = edge + LIFT_BELOW
    layout.modules.append(("P_Modul_03", sector, [module[0], y, module[1]], turn_of(back_of(d))))
    layout.module_floors.append(layout.modules[-1])
    layout.points.append(list(module))
    for along, right in LIFT_WALLS:
        layout.points.append(list(at(p, d, LIFT_ENTRY + along, right)))
        layout.points.append(list(module))
    layout.points.append([module[0], module[1], y + LIFT_RIDE, 0])
    length = LIFT_ENTRY + LIFT_EXIT
    out = at(p, d, length)
    layout.floors = part.grid(out)
    part.extras.append((funnel, out, d, y + LIFT_TOP, None))
    track.p = at(p, d, length + FUNNEL)
    track.h = y + LIFT_TOP - RISE
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "module"
    return h, h, length + FUNNEL


def lay_flaps(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"module": "P_Modul_19"}: the flaps with both roads they bridge, as
    Level_09 has them (see FLAPS_* in ballance.game.measures), entered from
    a narrow road through a funnel onto the flat road to the gap, and left
    where it is a narrow road's V again, on a grid of its own."""
    if track.width != NARROW or track.floor not in ("road", "trafo"):
        raise ValueError("the flaps are entered from a narrow road")
    check_ball(layout, "P_Modul_19")
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    start = FUNNEL  # where the cut road begins
    layout.open(layout.floors, p, d)
    part.extras.append((funnel, at(p, d, start), back_of(d), edge, None))
    along, right = FLAPS_MODULE
    module = at(p, d, start + along, right)
    layout.modules.append(("P_Modul_19", sector, [module[0], edge, module[1]], turn_of(back_of(d))))
    layout.module_floors.append(layout.modules[-1])
    along, right, above = FLAPS_COUNTERWEIGHT
    ball = at(p, d, start + along, right)
    layout.elements.append(("P_Ball_Stone", sector, [ball[0], edge + above, ball[1]]))
    track.p = at(p, d, start + FLAPS_LENGTH)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "module"
    return h, h, start + FLAPS_LENGTH


def lay_pushers(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"module": "P_Modul_01"}: a pair of pushers with their plaza, as
    Level_01 has them (see PUSHERS_* in ballance.game.measures), entered from
    a narrow road through a funnel onto the corridor, whose far end flattens
    into a narrow road's V, where the road goes on on a grid of its own."""
    if track.width != NARROW or track.floor != "road":
        raise ValueError("the pushers are entered from a narrow road")
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    layout.open(layout.floors, p, d)
    part.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge, None))
    # the pushers' X runs across the track, to its left
    for along in PUSHERS_MODULES:
        where = at(p, d, FUNNEL + along, PUSHERS_ASIDE)
        layout.modules.append(("P_Modul_01", sector, [where[0], edge, where[1]],
                               turn_of(left_of(d))))
    layout.module_floors.append(layout.modules[-len(PUSHERS_MODULES)])
    for along, right, hold in PUSHERS_PATH:
        where = at(p, d, FUNNEL + along, right)
        layout.points.append([where[0], where[1], h - 20.0, hold] if hold else list(where))
    track.p = at(p, d, FUNNEL + PUSHERS_LENGTH)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "module"
    return h, h, FUNNEL + PUSHERS_LENGTH


def lay_switch(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"module": "P_Modul_37", "side": "left" or "right"}: the switch, as
    Level_05 has it (see SWITCH_* in ballance.game.measures).  A square area
    the track enters and leaves straight on toward the cross piece, and a spur
    from its side that turns twice to end at the target; the ball goes down
    the spur, pushes the target until the plank falls, comes back and goes on
    over the plank."""
    if track.floor != "road":
        raise ValueError("the switch is entered from a narrow road")
    check_ball(layout, "P_Modul_37")
    side = piece.get("side")
    if side not in ("left", "right"):
        raise ValueError("P_Modul_37 needs a side for its spur, left or right")
    sign = 1 if side == "right" else -1
    p, d, h = track.p, track.d, track.h
    out = side_of(d, side)  # toward the spur
    back_out = back_of(out)

    def at_(a: float, r: float) -> tuple[float, float]:
        return at(p, d, a, sign * r)

    floors, edge = layout.floors, h + RISE
    half = SWITCH_AREA / 2
    cross = SWITCH_AREA + 10.0 + SWITCH_CROSS  # the cross piece's middle, along
    # the area, the track on to the cross piece, and the spur's first leg and
    # turn, on the grid behind
    floors.area(*rect(p, d, SWITCH_AREA, SWITCH_AREA), h)
    floors.road(*rect(at_(SWITCH_AREA, 0), d, 10.0, NARROW), h, along=axis_of(d))
    layout.open(floors, at_(SWITCH_AREA + 10.0, 0), d)
    leg = SWITCH_SPUR - NARROW / 2 - half
    floors.road(*rect(at_(half, half), out, leg, NARROW), h, along=axis_of(out))
    floors.road(*rect(at_(half, half + leg), out, NARROW, NARROW), h, along=axis_of(out))
    grid_end = half + NARROW / 2 + CELL * math.floor(
        (cross - NARROW / 2 - half - NARROW / 2) / CELL)
    floors.road(*rect(at_(half + NARROW / 2, SWITCH_SPUR), d, grid_end - half - NARROW / 2,
                      NARROW), h, along=axis_of(d))
    layout.open(floors, at_(grid_end, SWITCH_SPUR), d)
    # off the grid, up to the spur's second turn
    turn = cross - NARROW / 2
    part.extras.append((channel, at_(grid_end, SWITCH_SPUR), d, turn - grid_end, edge))
    spur = part.grid(at_(turn, SWITCH_SPUR - NARROW / 2))
    spur.road(*rect(at_(turn, SWITCH_SPUR), d, NARROW, NARROW), h, along=axis_of(d))
    layout.open(spur, at_(turn, SWITCH_SPUR), d)
    last = SWITCH_SPUR - NARROW / 2 - (SWITCH_TARGET + FUNNEL)
    spur.road(*rect(at_(cross, SWITCH_SPUR - NARROW / 2), back_out, last, NARROW), h,
              along=axis_of(out))
    layout.open(spur, at_(cross, SWITCH_TARGET + FUNNEL), back_out)
    part.extras.append((funnel, at_(cross, SWITCH_TARGET), out, edge, None))
    where = at_(cross, SWITCH_MODULE)
    layout.modules.append(("P_Modul_37", sector, [where[0], edge, where[1]], turn_of(out)))
    layout.module_floors.append(layout.modules[-1])
    # down the spur, push the target, back, and on over the plank
    corner1, corner2 = at_(half, SWITCH_SPUR), at_(cross, SWITCH_SPUR)
    target = at_(cross, SWITCH_TARGET - 4.0)
    layout.points += [list(at_(half, 0)), list(corner1), list(corner2),
                      [target[0], target[1], h - 10.0, SWITCH_HOLD],
                      list(corner2), list(corner1), list(at_(half, 0)), list(at_(cross, 0))]
    track.p = at_(cross + SWITCH_CROSS, 0)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "module"
    return h, h, SWITCH_AREA


# The pieces, by the key that names them.
PIECES: dict[str, Callable] = {
    "road": lay_road,
    "rail": lay_rail,
    "rail_turn": lay_rail_turn,
    "fall": lay_fall,
    "area": lay_area,
    "turn": lay_turn,
    "trafo": lay_trafo,
    "plank": lay_plank,
    "module": lay_module,
}


def lay_piece(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> None:
    """Lays one piece of sector number sector, and the elements on it."""
    floors = layout.floors
    start, heading = track.p, track.d
    rail = "rail" in piece or "rail_turn" in piece
    if "fall" in piece and track.run is None:
        raise ValueError("the ball falls from a rail's end")
    if track.floor == "fall" and "area" not in piece:
        raise ValueError("a rail's fall lands in an area")
    if rail and track.run is None:
        # Rails meet floors as the originals' do: at a narrow road's end or
        # through a gap in an area's rim.
        if track.floor not in ("road", "area"):
            raise ValueError("a rail starts from a narrow road or an area")
        layout.dock(floors, track.p, track.d)
        track.run = [Ring(track.p[0], track.h + ABOVE_V, track.p[1], track.d[0], track.d[1])]
    elif not rail and "fall" not in piece and track.run is not None:
        if not ("area" in piece or ("road" in piece and
                                    piece.get("width", "narrow") == "narrow")):
            raise ValueError("a rail ends at a narrow road or an area")
        layout.dock(floors, track.p, track.d)
        part.rails.runs.append(track.run)
        track.run = None
    if rail and piece.get("elements"):
        raise ValueError("elements stand on floors, not on rails")
    kind = next((key for key in PIECES if key in piece), None)
    if kind is None:
        raise ValueError(f"unknown piece {piece}")
    ramp = PIECES[kind](layout, sector, part, piece, track)
    for element in piece.get("elements", []):
        place_element(layout, sector, floors, piece, element, start, heading, ramp)


def place_element(layout: Layout, sector: int, floors, piece: dict, element: dict,
                  start, heading, ramp: Ramp) -> None:
    """{"kind": kind, "at": [along, right]}: an element on the piece, resting
    on the floor there; a fan (P_Modul_18) stands in a housing set into an
    area's flat inside, two cells square, its rim flush with the floor."""
    a, c = (float(v) for v in element["at"])
    y0, y1, length = ramp
    x, z = at(start, heading, a, c)
    floor = y0 + (y1 - y0) * min(max(a / length, 0.0), 1.0)
    kind = element["kind"]
    if kind == "P_Modul_18":
        if "area" not in piece:
            raise ValueError("a fan stands in an area")
        floors.hole(x - NARROW / 2, z - NARROW / 2, x + NARROW / 2, z + NARROW / 2)
        layout.housings.append((sector, [x, floor - FAN_BELOW, z]))
    layout.elements.append((kind, sector, [x, floor + ELEMENT_LIFT[kind], z]))
