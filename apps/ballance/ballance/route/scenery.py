"""The scenery around a laid-out route, placed as the original levels place
it (see the scenery in ballance.game.measures): lantern towers beside the
start, the checkpoints and the balloon, and striped poles by the balloon,
none where it would stand on a floor."""

from __future__ import annotations

import math

from ..core.geometry import at
from ..game.measures import (BALLOON_STICKS, BALLOON_TOWERS, CHECKPOINT_TOWERS, START_OPENING,
                             START_TOWERS, STICK_HALF, TOWER_HALF)
from ..kit.floors import CELL
from .layout import Layout


def on_floor(layout: Layout, x: float, z: float, half: float) -> bool:
    """Whether a square of half width half at (x, z) lies over a floor cell,
    or the start's or a checkpoint's floor."""
    for floors in (f for sector in layout.sectors for f in sector.floors):
        for i, j in floors.cells:
            cx, cz = floors.ox + (i + 0.5) * CELL, floors.oz + (j + 0.5) * CELL
            if abs(cx - x) < half + CELL / 2 and abs(cz - z) < half + CELL / 2:
                return True
    for marker in [(layout.start[0], 0.0, layout.start[1])] + [m for m, _ in layout.checkpoints]:
        if math.hypot(marker[0] - x, marker[2] - z) < START_OPENING + half + 2:
            return True
    return False


def towers(layout: Layout) -> list[list[float]]:
    """Where the lantern towers stand: the tops of the pillars they stand on."""
    spots = [(layout.start, layout.marker_y, layout.heading, START_TOWERS, 1)]
    for marker, d in layout.checkpoints:
        spots.append(((marker[0], marker[2]), marker[1], d, CHECKPOINT_TOWERS, 1))
    if layout.balloon is not None:
        where, d = layout.balloon
        spots.append(((where[0], where[2]), where[1], d, BALLOON_TOWERS, -1))
    out = []
    for (px, pz), y, d, placed, sign in spots:
        for a, c, up in placed:
            x, z = at((px, pz), d, sign * a, c)
            if not on_floor(layout, x, z, TOWER_HALF):
                out.append([x, y + up, z])
    return out


def sticks(layout: Layout) -> list[list[float]]:
    """Where the balloon's striped poles stand: their tops' middles."""
    if layout.balloon is None:
        return []
    where, d = layout.balloon
    out = []
    for a, c, up in BALLOON_STICKS:
        x, z = at((where[0], where[2]), d, -a, c)
        if not on_floor(layout, x, z, STICK_HALF):
            out.append([x, where[1] + up, z])
    return out
