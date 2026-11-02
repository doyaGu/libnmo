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

from ..core.geometry import (Heading, Point, across, at, axis_of, back_of, left_of, rect,
                             right_of, side_of, turn_of)
from ..game.measures import (ARMS_ABOVE, ARMS_BRIDGE, ARMS_LENGTH, ARMS_MODULE, ARMS_PATH, BOX_PILE,
                             BRIDGES, CLIMB_DROP, CLIMB_END, CLIMB_FANS, CLIMB_HOLD, CLIMB_LENGTH,
                             CLIMB_TOPS, CLIMB_TRAFO, CRADLE_BALL, CRADLE_EXIT, CRADLE_RING,
                             CRADLE_TOP, ELEMENT_LIFT, FAN_BELOW, FAN_BLOCK_FAN, FAN_BLOCK_RING,
                             FAN_BLOCK_TOP, FAN_REACH, FAN_TOWER_FLOOR, FAN_TOWER_HALF,
                             FAN_TOWER_HOLD, FAN_TOWER_PILLAR, FAN_TOWER_RING, FAN_TOWER_ROCK,
                             FLAPS_COUNTERWEIGHT, FLAPS_LENGTH, FLAPS_MODULE, FUNNEL, HOLED_TROUGH,
                             JUMP_ABOVE, JUMP_AREA, JUMP_DOWN, JUMP_HALF, LIFT_BELOW, LIFT_ENTRY,
                             LIFT_EXIT, LIFT_RIDE, LIFT_TOP, LIFT_WALLS, LOG_CROSS, LOG_MOST,
                             LOG_SEGMENT, LOG_TOP, PILLAR_INSET, PLANK_BELOW, PLANK_LENGTH,
                             PUSHERS_ASIDE, PUSHERS_LENGTH, PUSHERS_MODULES, PUSHERS_PATH,
                             RAILING_ARM, RAILING_BRIDGE, RAILING_INSET, RAIL_DOCK_BELOW,
                             RAIL_DOCK_LENGTH, RAIL_FALL, RAIL_OVERHANG, SACK_ABOVE, SPIRAL_OUT,
                             SPIRAL_PATH, STONEBOX_LENGTH, STONEBOX_MODULE, STONEBOX_PATH,
                             STONEBOX_RISE, STONEBOX_TRAFO, SWING_ABOVE, SWING_FAR, SWING_GO,
                             SWING_HALF, SWING_SEAT, SWING_WAIT, SWITCH_AREA, SWITCH_CROSS,
                             SWITCH_HOLD, SWITCH_MODULE, SWITCH_SPUR, SWITCH_TARGET, TRAFO_LIFT,
                             TROUGH_ASIDE, TROUGH_BOTTOM, TROUGH_DODGE, TROUGH_HOLES,
                             TROUGH_HOLES_FROM, TROUGH_HOP, TROUGH_LAND, TROUGH_LOG_GAP,
                             TROUGH_SEGMENT, TWIST_ASIDE, TWIST_BELOW, TWIST_HALF, TWIST_PATH,
                             WALKWAY_ABOVE, WALKWAY_LENGTH, WALKWAY_LONG)
from ..kit.floors import CELL, NARROW, RISE, WALL
from ..kit.pieces import channel, funnel, pad, strip
from ..kit.railings import OUT
from ..kit.rails import ABOVE_V, BEND_RADIUS, Ring, bend
from .modules import MODULE_FLOORS, MODULES, NEEDS_BALL, module_layout

if TYPE_CHECKING:
    from .layout import Layout, Sector, Track

Ramp = tuple[float, float, float]  # height at the start, at the end, length

WIDTHS = {"narrow": NARROW, "wide": 2 * NARROW}
# A rail bend, with its straights, ends 10 along and 10 aside: on the grid.
BEND_SPAN = 4 * CELL


def lay_road(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"road": length, "width": "narrow" or "wide", "drop": dy, "railings":
    "left", "right" or "both"}: with railings, they run along its sides."""
    length = float(piece["road"])
    width = WIDTHS[piece.get("width", "narrow")]
    end = track.h - float(piece.get("drop", 0.0))
    layout.floors.road(*rect(track.p, track.d, length, width), track.h, end,
                       along=axis_of(track.d))
    if piece.get("railings"):
        lay_side_railings(layout, sector, part, track.p, track.d, length, width,
                          (track.h + RISE, end + RISE), piece["railings"])
    ramp = (track.h, end, length)
    track.p, track.h, track.width = at(track.p, track.d, length), end, width
    layout.points.append(list(track.p))
    track.floor = "road" if width == NARROW else "wide"
    return ramp


def lay_rail(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> None:
    """{"rail": length, "drop": dy, "railings": true}: with railings, the
    rail crosses a gap between narrow roads with railings either side, as
    Level_05's A01_Rail_01 does, from RAILING_BRIDGE back on the road before
    to as far on the road after."""
    length = float(piece["rail"])
    if piece.get("railings"):
        last = layout.parts[-1]
        if last.what != "road" or track.width != NARROW or \
                _road_length(last) < RAILING_BRIDGE:
            raise ValueError(f"a rail with railings leaves a narrow road {RAILING_BRIDGE:g} "
                             "long at least")
        drop = float(piece.get("drop", 0.0))
        lay_side_railings(layout, sector, part, at(track.p, track.d, -RAILING_BRIDGE), track.d,
                          length + 2 * RAILING_BRIDGE, NARROW,
                          (track.h + RISE, track.h - drop + RISE), "both", 0.0)
        track.railed = RAILING_BRIDGE
    track.p = at(track.p, track.d, length)
    track.h -= float(piece.get("drop", 0.0))
    track.run.append(Ring(track.p[0], track.h + ABOVE_V, track.p[1], track.d[0], track.d[1]))
    layout.points.append(list(track.p))
    track.floor = "rail"


def _road_length(part) -> float:
    """How long the road a part laid is."""
    _, piece = part.floors[-1]
    i0, j0, i1, j1 = piece.rect
    return CELL * (i1 - i0 if piece.axis == 0 else j1 - j0)


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
    """{"fall": dy, "split": true}: the rail ends in the air, and the ball
    falls dy onto the area that comes next, which starts RAIL_OVERHANG back
    under the rail's end (as Level_01's rails drop the ball into its pits);
    with split, the rail's tubes part there, the ball dropping between them,
    as Level_01's A01_Rail_02_01 do."""
    fall = float(piece["fall"])
    if not 0 < fall <= RAIL_FALL:
        raise ValueError(f"a rail drops the ball at most {RAIL_FALL}")
    if track.run is None:
        # off a log's or a trough's end
        track.h -= fall
        track.p = at(track.p, track.d, -RAIL_OVERHANG)
        track.floor = "fall"
        track.off_floor = True
        return
    track.off_floor = False
    if piece.get("split"):
        end = track.run[-1]
        layout.module_floors.append(("Rail_Split", sector, [end.x, end.y, end.z],
                                     turn_of(back_of(track.d))))
    part.rails.runs.append(track.run)
    track.run = None
    track.h -= fall
    track.p = at(track.p, track.d, -RAIL_OVERHANG)
    track.floor = "fall"


def lay_tower(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"tower": true}: the rail ends in Level_04's fan tower (see FAN_TOWER_* in
    ballance.game.measures): it bends up round a fan into a shaft the fan
    blows the ball up, and a cap throws the ball back over the rail onto an
    area round the shaft, which comes next."""
    if layout.last_ball != "Paper":
        layout.warn("only a paper ball rides a fan's wind; the ball reaches it as "
                    f"{layout.last_ball.lower()}")
    end, d = track.run[-1], track.d
    part.rails.runs.append(track.run)
    track.run = None
    x, y, z = FAN_TOWER_RING
    fan = at((end.x, end.z), d, -z, -x)
    above = end.y - y
    # the fan's X to the track's right, its Z along it
    layout.module_floors.append(("FanTower", sector, [fan[0], above, fan[1]],
                                 turn_of(right_of(d))))
    layout.elements.append(("P_Modul_18", sector, [fan[0], above, fan[1]]))
    layout.pillars.append([fan[0], above + FAN_TOWER_PILLAR, fan[1]])
    track.d, track.h = back_of(d), above + FAN_TOWER_FLOOR
    # flat out from the rail's end into the bend (braking, it stops at the
    # bottom), and over the fan until the ball is above the floor round the
    # shaft
    layout.points[-1] = layout.points[-1][:2] + [None, "rush"]
    layout.points.append(list(fan) + [None, "rush"])
    # The fan blows only once the ball's bounding box, which grows and
    # shrinks as the ball turns, reaches its wind; resting in the bend the
    # ball is just short of it, so it is rocked across the rail to turn it.
    for aside in (FAN_TOWER_ROCK, -FAN_TOWER_ROCK, FAN_TOWER_ROCK, -FAN_TOWER_ROCK):
        layout.points.append(list(at(fan, d, 0.0, aside)) + [None, FAN_TOWER_HOLD])
    layout.points.append([fan[0], fan[1], track.h + 2 * RISE + 4.0])
    track.p = at(fan, track.d, FAN_TOWER_HALF)
    layout.floors = part.grid(track.p)
    layout.points.append(list(at(track.p, track.d, NARROW)))
    track.floor = "tower"
    return track.h, track.h, NARROW


def lay_log(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"log": length}: a log the ball lands on from a rail's fall, or
    {"log": length, "turn": "left" or "right", "drop": dy} one crossing dy
    under the last log, which the ball rolls off onto it (as Level_12's
    A08_Halfpipe_* do; see LOG_* in ballance.game.measures).  The length is
    a whole number of segments."""
    length = float(piece["log"])
    segments = round(length / LOG_SEGMENT)
    if segments < 1 or abs(segments * LOG_SEGMENT - length) > 1e-6:
        raise ValueError(f"a log is a whole number of {LOG_SEGMENT:g}-long segments")
    if track.floor == "fall":
        d, top = track.d, track.h
        land = at(track.p, d, RAIL_OVERHANG)
        start = at(land, d, -LOG_CROSS)
    elif track.floor == "trough":
        # on out of the trough's open end, level with its bottom
        d, top = track.d, track.h
        start = at(track.p, d, TROUGH_LOG_GAP)
    else:
        if "turn" not in piece or "drop" not in piece:
            raise ValueError("a log after a log turns and drops under it")
        cross = at(track.p, track.d, -LOG_CROSS)
        d, top = side_of(track.d, piece["turn"]), track.h - float(piece["drop"])
        start = at(cross, d, -LOG_CROSS)
    if segments > LOG_MOST:
        raise ValueError(f"a log is {LOG_MOST} segments long at most")
    middle = at(start, d, length / 2)
    layout.module_floors.append((f"Log_{segments}", sector, [middle[0], top - LOG_TOP, middle[1]],
                                 turn_of(d)))
    end = at(start, d, length)
    if piece.get("climb"):
        end, top = lay_climb(layout, sector, end, d, top)
        length += CLIMB_LENGTH
    layout.points.append(list(at(end, d, -LOG_CROSS)))
    track.p, track.d, track.h, track.floor = end, d, top, "log"
    return top, top, length


def lay_climb(layout: Layout, sector: int, p: Point, d: Heading, top: float
              ) -> tuple[Point, float]:
    """Level_06's logs climbing with fans from a log's end p, its top at top
    (see CLIMB_* in ballance.game.measures); the last log's end and top."""
    if layout.last_ball != "Paper":
        layout.warn("only a paper ball rides the climbing logs' fans; the ball reaches them "
                    f"as {layout.last_ball.lower()}")
    # the logs' Z back along the track
    layout.module_floors.append(("LogClimb", sector, [p[0], top, p[1]], turn_of(left_of(d))))
    for (along, up), above in zip(CLIMB_FANS, CLIMB_TOPS):
        fan = at(p, d, along)
        layout.elements.append(("P_Modul_18", sector, [fan[0], top + up, fan[1]]))
        # over the fan until the ball is above the log the fan blows it onto
        layout.points.append([fan[0], fan[1], top + above + 2.0])
    along, up = CLIMB_TRAFO
    trafo = at(p, d, along)
    layout.elements.append(("P_Trafo_Wood", sector, [trafo[0], top + up, trafo[1]]))
    layout.trafos.add("Wood")
    layout.last_ball = "Wood"
    layout.points.append(list(trafo))
    along, up = CLIMB_DROP
    hole = at(p, d, along)
    layout.elements.append(("P_Modul_18", sector, [hole[0], top + up, hole[1]]))
    # held over the hole until the ball drops through it
    layout.points.append([hole[0], hole[1], None, CLIMB_HOLD])
    return at(p, d, CLIMB_LENGTH), top + CLIMB_END


def lay_trough(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"trough": length}: a trough the ball drops into from a fall (under a
    rail's end, or past a log's), or
    {"trough": length, "hop": "left" or "right"} one beside the last, which
    the ball swings over into (as Level_04's A01_Halfpipe_* are; see
    TROUGH_* in ballance.game.measures).  The length is a whole number of
    segments, and a trough hopped from is 100 long at least.  With "holes":
    true, it is Level_04's trough with holes in its bottom, 350 long."""
    length = float(piece["trough"])
    segments = round(length / TROUGH_SEGMENT)
    if segments < 1 or abs(segments * TROUGH_SEGMENT - length) > 1e-6:
        raise ValueError(f"a trough is a whole number of {TROUGH_SEGMENT:g}-long segments")
    d = track.d
    if track.floor == "fall":
        bottom = track.h
        # under a rail's end; past a log's or a trough's, whose body would
        # stand in its walls
        start = at(track.p, d, RAIL_OVERHANG - (0.0 if track.off_floor else TROUGH_LAND))
    else:
        if track.floor != "trough" or "hop" not in piece:
            raise ValueError("a trough after a trough is hopped into, to its left or right")
        if track.trough < 2 * TROUGH_SEGMENT:
            raise ValueError("a trough hopped from is 100 long at least")
        check_ball(layout, "Trough")
        sign = 1 if piece["hop"] == "right" else -1
        bottom = track.h
        start = at(track.p, d, -TROUGH_SEGMENT, sign * TROUGH_ASIDE)
        back = at(track.p, d, -TROUGH_SEGMENT)  # the next trough's start, in the last
        for along, aside, rush in TROUGH_HOP:
            p = at(back, d, along, sign * aside)
            layout.points.append(list(p) + [None, "rush"] if rush else list(p))
    if segments > LOG_MOST:
        raise ValueError(f"a trough is {LOG_MOST} segments long at most")
    holes = piece.get("holes", False)
    if holes and length != HOLED_TROUGH:
        raise ValueError(f"the trough with holes is {HOLED_TROUGH:g} long")
    middle = at(start, d, length / 2)
    layout.module_floors.append(("Trough_Holes" if holes else f"Trough_{segments}", sector,
                                 [middle[0], bottom + TROUGH_BOTTOM, middle[1]], turn_of(d)))
    if holes:
        layout.points.append(list(at(start, d, TROUGH_HOLES_FROM)))
        for k, hole in enumerate(TROUGH_HOLES):
            layout.points.append(list(at(start, d, hole, TROUGH_DODGE * (-1) ** k)))
    end = at(start, d, length)
    track.p, track.h, track.floor, track.trough = end, bottom, "trough", length
    return bottom, bottom, length


def lay_fan(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"fan": dy}: the rail ends in a fan's housing block, as Level_05's and
    Level_13's do (see FAN_BLOCK_* in ballance.game.measures), and the fan
    lifts the ball onto the next piece, which starts at the block's far side
    dy above its top."""
    rise = float(piece["fan"])
    if not 0 < rise <= FAN_REACH:
        raise ValueError(f"a fan lifts the ball at most {FAN_REACH}")
    if layout.last_ball != "Paper":
        layout.warn("only a paper ball rides a fan's wind; the ball reaches it as "
                    f"{layout.last_ball.lower()}")
    end, d = track.run[-1], track.d
    part.rails.runs.append(track.run)
    track.run = None
    middle = at((end.x, end.z), d, NARROW / 2)
    bottom = end.y - FAN_BLOCK_RING
    # the block's X to the track's left, its Z back along it
    layout.module_floors.append(("FanBlock", sector, [middle[0], bottom, middle[1]],
                                 turn_of(left_of(d))))
    layout.elements.append(("P_Modul_18", sector, [middle[0], bottom + FAN_BLOCK_FAN, middle[1]]))
    top = bottom + FAN_BLOCK_TOP
    # wait over the fan until the ball is above the floor ahead
    layout.points.append([middle[0], middle[1], top + rise + RISE + 1.0])
    track.p = at((end.x, end.z), d, NARROW)
    track.h = top + rise - RISE
    # the ball comes in over the next piece's rim, which stays closed
    layout.floors = part.grid(track.p)
    layout.points.append(list(track.p))
    track.floor = "area"
    return track.h, track.h, NARROW


def lay_cradle(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"cradle": true}: the rail bends straight down into the cradle, as
    Level_01's A01_Rail_09 does (see CRADLE_* in ballance.game.measures),
    and the track leaves it to the right, from a narrow road's end on its
    block."""
    check_ball(layout, "Cradle")
    end, d = track.run[-1], track.d
    back, above = CRADLE_BALL
    layout.elements.append(("P_Ball_Wood", sector,
                            [end.x - back * d[0], end.y + above, end.z - back * d[1]]))
    part.rails.runs.append(track.run)
    track.run = None
    x, y, z = CRADLE_RING
    left, right = left_of(d), right_of(d)
    # the block's X to the track's left, its Z back along it
    middle = (end.x - x * left[0] + z * d[0], end.z - x * left[1] + z * d[1])
    below = end.y - y
    layout.module_floors.append(("Cradle", sector, [middle[0], below, middle[1]], turn_of(left)))
    layout.points.append(list(middle))
    track.p = (middle[0] + CRADLE_EXIT * right[0], middle[1] + CRADLE_EXIT * right[1])
    track.d = right
    track.h = below + CRADLE_TOP - RISE
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, track.d)
    layout.points.append(list(track.p))
    track.floor = "road"
    return track.h, track.h, 0.0


def lay_area(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"area": [along, across], "exit": "straight", "left" or "right",
    "rise": dy, "railings": true, "branches": [...], "name": NAME}; with
    railings, Level_01's railing stands round each of its corners (as
    Level_05's do); with a rise, a fan among its elements lifts the ball onto
    the next piece, dy higher, on a grid of its own (as Level_03's do).
    After a rail's fall it starts on a grid of its own, its rim closed.

    A branch ({"side": ..., "route": [pieces]}) leaves a side the track
    doesn't, as the originals' spurs and second ways do (ballance.route.layout
    lays it); a named area is one a branch may come back to."""
    along, across = (float(v) for v in piece["area"])
    p, d, h = track.p, track.d, track.h
    if track.floor == "fall":
        layout.floors = part.grid(p)
    layout.floors.area(*rect(p, d, along, across), h)
    if piece.get("railings"):
        if min(along, across) < 2 * RAILING_ARM + NARROW - 1e-6:
            raise ValueError(f"an area with railings is {2 * RAILING_ARM + NARROW:g} "
                             "across at least, for a road between them")
        for a, c in ((0.0, -1), (0.0, 1), (along, -1), (along, 1)):
            into_along = d if a == 0.0 else back_of(d)
            into_across = right_of(d) if c < 0 else left_of(d)
            lay_corner_railing(layout, sector, part, at(p, d, a, c * across / 2),
                               (into_along, into_across), h + RISE)
    for a in (PILLAR_INSET, along - PILLAR_INSET):
        for c in (-across / 2 + PILLAR_INSET, across / 2 - PILLAR_INSET):
            x, z = at(p, d, a, c)
            layout.pillars.append([x, h + RISE - WALL, z])
    exit = piece.get("exit", "straight")
    middle = at(p, d, along / 2)
    # the middles of its sides, each with the way out of it
    sides = {"straight": (at(p, d, along), d),
             "left": (at(middle, left_of(d), across / 2), left_of(d)),
             "right": (at(middle, right_of(d), across / 2), right_of(d))}
    if exit not in sides:
        raise ValueError(f"an area's exit is straight, left or right, not {exit}")
    used = {exit}
    for branch in piece.get("branches", []):
        side = branch.get("side")
        if side not in sides or side in used:
            raise ValueError(f"a branch leaves a side of an area no track leaves (straight, "
                             f"left or right), not {side}")
        if piece.get("rise"):
            raise ValueError("a branch leaves an area that doesn't rise")
        used.add(side)
        track.forks.append((*sides[side], h, layout.floors, branch))
    if "name" in piece:
        layout.name_node(piece["name"], layout.floors, h,
                         [place for side, place in sides.items() if side not in used])
    track.p, track.d = sides[exit]
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


def lay_railing(layout: Layout, sector: int, part: Sector, posts: list[Point],
                rims: list[float], inward: tuple[Heading, Heading]) -> None:
    """A railing whose top runs over posts (the first and last its posts,
    the rest its corners), outside floor walls whose rims are at rims there,
    each post clamped to its wall: inward from the first post and from the
    last lies the floor."""
    part.railings.append([(x, rim, z) for (x, z), rim in zip(posts, rims)])
    for (x, z), rim, into in ((posts[0], rims[0], inward[0]), (posts[-1], rims[-1], inward[1])):
        # the clamp's wall along its X, the floor toward its Z
        layout.module_floors.append(("Clamp", sector, [x, rim, z], turn_of(right_of(into))))


def lay_corner_railing(layout: Layout, sector: int, part: Sector, corner: Point,
                       inward: tuple[Heading, Heading], rim: float) -> None:
    """Level_01's railing round a floor's corner, whose walls meet at corner
    and whose floor lies inward along both headings, its rim at rim."""
    u, v = inward
    cx, cz = corner

    def point(along_u: float, along_v: float) -> Point:
        return cx + u[0] * along_u + v[0] * along_v, cz + u[1] * along_u + v[1] * along_v

    # outside both walls, a post along each
    posts = [point(RAILING_ARM, -OUT), point(-OUT, -OUT), point(-OUT, RAILING_ARM)]
    lay_railing(layout, sector, part, posts, [rim] * 3, (v, u))


def lay_side_railings(layout: Layout, sector: int, part: Sector, start: Point, d: Heading,
                      length: float, width: float, rims: tuple[float, float], sides: str,
                      inset: float = RAILING_INSET) -> None:
    """Railings along a road's sides ("left", "right" or "both"), from
    start along d for length, outside its walls width apart, their posts
    inset from the ends, the rims at the ends at rims."""
    for sign, side in ((1, "right"), (-1, "left")):
        if sides in (side, "both"):
            aside = sign * (width / 2 + OUT)
            posts = [at(start, d, inset, aside), at(start, d, length - inset, aside)]
            into = left_of(d) if sign > 0 else right_of(d)
            lay_railing(layout, sector, part, posts, list(rims), (into, into))


def lay_turn(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"turn": "left" or "right"}: a square corner as wide as the road; with
    "railing": true, a narrow road's has a railing round its outer corner,
    as Level_01's and Level_05's do."""
    width = track.width
    layout.floors.road(*rect(track.p, track.d, width, width), track.h, track.h,
                       along=axis_of(track.d))
    if piece.get("railing"):
        if width != NARROW:
            raise ValueError("a railing stands round a narrow road's corner")
        side = side_of(track.d, piece["turn"])
        outer = at(track.p, track.d, width, width / 2 * (-1 if side == right_of(track.d) else 1))
        lay_corner_railing(layout, sector, part, outer, (back_of(track.d), side),
                           track.h + RISE)
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
    part.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge, None, True))
    middle = at(p, d, FUNNEL + PLANK_LENGTH / 2)
    layout.module_floors.append(("Modul27", sector, [middle[0], edge + PLANK_BELOW, middle[1]],
                                 turn_of(d)))
    length = FUNNEL + PLANK_LENGTH
    part.extras.append((funnel, at(p, d, length), d, edge, None, True))
    track.p = at(p, d, length + FUNNEL)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "road"
    return h, h, length + FUNNEL


def lay_walkway(layout: Layout, sector: int, part: Sector, piece: dict,
                track: Track) -> Ramp:
    """{"walkway": true}: the wavy wood walkway, as Level_08 has it (see
    WALKWAY_* in ballance.game.measures), between funnels that flatten a
    narrow road to it and back; with "long": true, Level_11's longer one."""
    if track.width != NARROW:
        raise ValueError("the wood walkway lies on a narrow road")
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    layout.open(layout.floors, p, d)
    part.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge, None, True))
    if piece.get("long"):
        # Level_11's, along its Z
        kind, walkway, turn = "Modul42_Long", WALKWAY_LONG, turn_of(right_of(d))
    else:
        kind, walkway, turn = "Modul42", WALKWAY_LENGTH, turn_of(d)
    middle = at(p, d, FUNNEL + walkway / 2)
    layout.module_floors.append((kind, sector, [middle[0], edge - WALKWAY_ABOVE, middle[1]],
                                 turn))
    length = FUNNEL + walkway
    part.extras.append((funnel, at(p, d, length), d, edge, None, True))
    track.p = at(p, d, length + FUNNEL)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "road"
    return h, h, length + FUNNEL


def lay_twist(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"twist": true}: the wood twist with the floors at its mouths, as
    Level_07 has them (see TWIST_* in ballance.game.measures), between
    funnels that flatten a narrow road to them and back."""
    if track.width != NARROW:
        raise ValueError("the wood twist lies on a narrow road")
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    layout.open(layout.floors, p, d)
    part.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge, None))
    # its X to the track's right, its Z along it
    middle = at(p, d, FUNNEL + TWIST_HALF, -TWIST_ASIDE)
    layout.module_floors.append(("Modul11", sector, [middle[0], edge + TWIST_BELOW, middle[1]],
                                 turn_of(right_of(d))))
    for x, z in TWIST_PATH:
        layout.points.append(list(at(p, d, FUNNEL + TWIST_HALF + z, x - TWIST_ASIDE)))
    length = FUNNEL + 2 * TWIST_HALF
    part.extras.append((funnel, at(p, d, length), d, edge, None))
    track.p = at(p, d, length + FUNNEL)
    layout.floors = part.grid(track.p)
    layout.open(layout.floors, track.p, d)
    layout.points.append(list(track.p))
    track.floor = "road"
    return h, h, length + FUNNEL


def lay_spiral(layout: Layout, sector: int, part: Sector, piece: dict,
               track: Track) -> Ramp:
    """{"spiral": true}: Level_01's wood spiral, leaving a wide road to its
    right at its last 10 and coming out higher onto a wide road heading the
    same way (see SPIRAL_* in ballance.game.measures)."""
    if track.floor != "wide":
        raise ValueError("the wood spiral leaves a wide road")
    check_ball(layout, "Spiral")
    p, d, h = track.p, track.d, track.h
    layout.open(layout.floors, p, d, track.width)
    # its X along the track, its Z to the track's left
    layout.module_floors.append(("Spiral", sector, [p[0], h, p[1]], turn_of(d)))
    for along, right in SPIRAL_PATH:
        layout.points.append(list(at(p, d, along, right)))
    along, right, up = SPIRAL_OUT
    track.p, track.h = at(p, d, along, right), h + up
    # the spiral's end is level with the rim of the road it comes out on,
    # which stays closed (as Level_01's does)
    layout.floors = part.grid(track.p)
    layout.points.append(list(track.p))
    return h, h, along


def lay_jump(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> Ramp:
    """{"jump": true}: the wood jump and the area it lands in, as Level_09
    has them (see JUMP_* in ballance.game.measures), a funnel flattening a
    narrow road to its top and another the road out of the area; the track
    goes on JUMP_DOWN lower."""
    if track.width != NARROW:
        raise ValueError("the wood jump starts from a narrow road")
    check_ball(layout, "Modul02")
    p, d, h = track.p, track.d, track.h
    edge = h + RISE
    layout.open(layout.floors, p, d)
    part.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge, None, True))
    start = FUNNEL + JUMP_HALF  # the jump's middle
    middle = at(p, d, start)
    layout.module_floors.append(("Modul02", sector, [middle[0], edge - JUMP_ABOVE, middle[1]],
                                 turn_of(d)))
    near, far = JUMP_AREA
    # run at flat out down the slope and over the gap, as a player does
    layout.points.append(list(at(p, d, start + (near + far) / 2)) + [None, "rush"])
    length = start + far
    edge -= JUMP_DOWN
    part.extras.append((funnel, at(p, d, length), d, edge, None))
    track.p = at(p, d, length + FUNNEL)
    track.h = edge - RISE
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
    part.extras.append((funnel, at(p, d, FUNNEL), back_of(d), edge, None, True))
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
    "cradle": lay_cradle,
    "fan": lay_fan,
    "tower": lay_tower,
    "log": lay_log,
    "trough": lay_trough,
    "area": lay_area,
    "turn": lay_turn,
    "trafo": lay_trafo,
    "plank": lay_plank,
    "walkway": lay_walkway,
    "twist": lay_twist,
    "jump": lay_jump,
    "spiral": lay_spiral,
    "module": lay_module,
}


def lay_piece(layout: Layout, sector: int, part: Sector, piece: dict, track: Track) -> None:
    """Lays one piece of sector number sector, and the elements on it."""
    floors = layout.floors
    start, heading = track.p, track.d
    rail = "rail" in piece or "rail_turn" in piece
    if ("cradle" in piece or "fan" in piece or "tower" in piece) and track.run is None:
        raise ValueError("the ball comes into the cradle, the fan's block or the fan's tower "
                         "on a rail")
    if track.floor == "tower" and "area" not in piece:
        raise ValueError("the fan's tower throws the ball into an area")
    if track.railed:
        if "rail" in piece or "rail_turn" in piece:
            raise ValueError("a rail with railings goes straight on to a narrow road")
        if "road" not in piece or piece.get("width", "narrow") != "narrow" or \
                float(piece["road"]) < track.railed:
            raise ValueError(f"a rail with railings ends on a narrow road {track.railed:g} "
                             "long at least")
        track.railed = 0.0
    if "fall" in piece and track.run is None and track.floor not in ("log", "trough"):
        raise ValueError("the ball falls from a rail's, a log's or a trough's end")
    if track.floor == "fall" and not ("area" in piece or "log" in piece or "trough" in piece):
        raise ValueError("a fall lands in an area, on a log or in a trough")
    if track.floor == "log" and "log" not in piece and "fall" not in piece:
        raise ValueError("a log goes on to another log or a fall")
    if track.floor == "trough" and not ("trough" in piece or "log" in piece or "fall" in piece):
        raise ValueError("a trough goes on to another trough, a log or a fall")
    if rail and track.run is None:
        # Rails meet floors as the originals' do: at a narrow road's end,
        # through a gap in an area's rim, or through a wide road's dock.
        if track.floor == "wide":
            dock_wide(layout, sector, part, track)
        elif track.floor not in ("road", "area"):
            raise ValueError("a rail starts from a narrow road, a wide one or an area")
        else:
            layout.dock(floors, track.p, track.d)
            track.run = [Ring(track.p[0], track.h + ABOVE_V, track.p[1], track.d[0], track.d[1])]
    elif not rail and not ("fall" in piece or "cradle" in piece or "fan" in piece
                           or "tower" in piece) and track.run is not None:
        if "road" in piece and piece.get("width") == "wide":
            dock_wide(layout, sector, part, track)
        elif not ("area" in piece or "road" in piece):
            raise ValueError("a rail ends at a narrow road, a wide one or an area")
        else:
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


def dock_wide(layout: Layout, sector: int, part: Sector, track: Track) -> None:
    """A rail meets a wide road's end through the originals' dock (see
    RAIL_DOCK_* in ballance.game.measures): from the road onto a rail when
    no rail is being laid, else from the rail onto the road."""
    d = track.d
    if track.run is None:
        layout.floors.open_edge(*across(track.p, d, 2 * NARROW))
        # the dock's Z back along the track, from the road to the rail
        layout.module_floors.append(("RailDock", sector, [track.p[0], track.h, track.p[1]],
                                     turn_of(left_of(d))))
        track.p = at(track.p, d, RAIL_DOCK_LENGTH)
        track.h -= RAIL_DOCK_BELOW + ABOVE_V
        track.run = [Ring(track.p[0], track.h + ABOVE_V, track.p[1], d[0], d[1])]
        return
    end = track.run[-1]
    part.rails.runs.append(track.run)
    track.run = None
    track.p = at((end.x, end.z), d, RAIL_DOCK_LENGTH)
    track.h = end.y + RAIL_DOCK_BELOW
    # the dock's Z along the track, from the rail to the road
    layout.module_floors.append(("RailDock", sector, [track.p[0], track.h, track.p[1]],
                                 turn_of(right_of(d))))
    layout.floors = part.grid(track.p)
    layout.floors.open_edge(*across(track.p, d, 2 * NARROW))
    layout.points.append(list(track.p))
    track.width, track.floor = 2 * NARROW, "wide"


def place_element(layout: Layout, sector: int, floors, piece: dict, element: dict,
                  start, heading, ramp: Ramp) -> None:
    """{"kind": kind, "at": [along, right], "up": dy, "pile": n}: an element
    on the piece, resting on the floor there or dy above it (the originals
    hang extra points over a fan or a sack's swing); n boxes piled up, as
    Level_08's and Level_12's are.  A fan (P_Modul_18) stands in a housing
    set into an area's flat inside, two cells square, its rim flush with the
    floor."""
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
    pile = int(element.get("pile", 1))
    if pile > 1 and kind != "P_Box":
        raise ValueError("only boxes are piled up")
    up = float(element.get("up", 0.0))
    for k in range(pile):
        layout.elements.append((kind, sector,
                                [x, floor + ELEMENT_LIFT[kind] + up + k * BOX_PILE, z]))
