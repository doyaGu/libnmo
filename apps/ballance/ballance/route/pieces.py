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

from ..core.geometry import at, axis_of, back_of, rect, side_of, turn_of
from ..game.measures import (ELEMENT_LIFT, FAN_BELOW, FAN_REACH, FUNNEL, PILLAR_INSET,
                             SACK_ABOVE, SWITCH_AREA, SWITCH_CROSS, SWITCH_HOLD, SWITCH_MODULE,
                             SWITCH_SPUR, SWITCH_TARGET, TRAFO_LIFT)
from ..kit.floors import CELL, NARROW, RISE, WALL
from ..kit.pieces import channel, funnel, pad, strip
from ..kit.rails import ABOVE_V, BEND_RADIUS, Ring, bend
from .modules import MODULES, NEEDS_BALL, module_layout

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
    """{"rail_turn": "left" or "right"}"""
    sign = 1 if piece["rail_turn"] == "right" else -1
    side = side_of(track.d, piece["rail_turn"])
    a = at(track.p, track.d, BEND_SPAN - BEND_RADIUS)
    rings = bend(a[0], track.h + ABOVE_V, a[1], track.d, side)
    track.p = at(track.p, track.d, BEND_SPAN, sign * BEND_SPAN)
    track.d = side
    track.run += rings + [Ring(track.p[0], track.h + ABOVE_V, track.p[1], side[0], side[1])]
    layout.points += [[r.x, r.z] for r in rings[3::3]] + [list(track.p)]
    track.floor = "rail"


def lay_area(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"area": [along, across], "exit": "straight", "left" or "right",
    "rise": dy}; with a rise, a fan among its elements lifts the ball onto the
    next piece, dy higher, on a grid of its own (as Level_03's do)."""
    along, across = (float(v) for v in piece["area"])
    p, d, h = track.p, track.d, track.h
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
        ball, text = NEEDS_BALL[kind]
        if layout.last_ball != ball:
            layout.warn(f"a {layout.last_ball.lower()} ball " + text.format(kind=kind))


def lay_module(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"module": kind, "count": n}: a module, or a row of them, between
    funnels; the road beyond goes on on a grid of its own."""
    kind = piece["module"]
    if kind not in MODULES:
        raise ValueError(f"unknown module {kind}; known: {', '.join(MODULES)}")
    if kind == "P_Modul_37":
        return lay_switch(layout, sector, part, piece, track)
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
    # no point at the module itself: driving at it, the ball would slow down
    # where it has to push or roll over it
    layout.points.append(list(at(p, d, FUNNEL + length)))
    track.p = at(p, d, 2 * FUNNEL + length)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "module"
    return h, h, 2 * FUNNEL + length


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
    "area": lay_area,
    "turn": lay_turn,
    "trafo": lay_trafo,
    "module": lay_module,
}


def lay_piece(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> None:
    """Lays one piece of sector number sector, and the elements on it."""
    floors = layout.floors
    start, heading = track.p, track.d
    rail = "rail" in piece or "rail_turn" in piece
    if rail and track.run is None:
        # Rails meet floors as the originals' do: at a narrow road's end or
        # through a gap in an area's rim.
        if track.floor not in ("road", "area"):
            raise ValueError("a rail starts from a narrow road or an area")
        layout.dock(floors, track.p, track.d)
        track.run = [Ring(track.p[0], track.h + ABOVE_V, track.p[1], track.d[0], track.d[1])]
    elif not rail and track.run is not None:
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
