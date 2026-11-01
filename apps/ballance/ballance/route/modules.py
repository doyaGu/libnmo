"""The modules (P_Modul_*) a route lays on the track, and the floors they
come with."""

from __future__ import annotations

from ..core.obj import Box
from ..game.catalog import STONE_FLOOR_GROUPS, STOPPER_GROUPS
from ..game.measures import (DRAWBRIDGE_FLOORS, DRAWBRIDGE_HINGE, SACK_STEP, SLAB, SLAB_GAP,
                             SWITCH_CROSS)

# The modules that stand on the track, and how many in a row.  Each but the
# switch stands between funnels that lead a narrow road onto it at the road's
# edge height and off it again.
MODULES = {"P_Modul_25": (1, 1), "P_Modul_26": (1, 4), "P_Modul_37": (1, 1),
           "P_Modul_41": (2, 8)}

# The modules only a paper ball, or a stone ball, gets past: (ball, what
# happens with another).
NEEDS_BALL = {
    "P_Modul_41": ("Paper", "tips {kind}'s slabs and falls; only a paper ball crosses them"),
    "P_Modul_37": ("Stone", "does not tip {kind}'s plank; a stone ball does"),
}


def module_layout(kind: str, count: int) -> tuple[float, list[float], bool]:
    """The length between a module's funnels, where each module stands from
    their start, and whether the funnels end in walls (closing them where
    they stop at a gap)."""
    if kind == "P_Modul_25":
        return DRAWBRIDGE_FLOORS, [DRAWBRIDGE_HINGE], False
    if kind == "P_Modul_26":
        return SACK_STEP * count, [SACK_STEP * (k + 0.5) for k in range(count)], False
    step = SLAB + SLAB_GAP
    return step * count + SLAB_GAP, [SLAB_GAP + SLAB / 2 + step * k for k in range(count)], True


class ModuleFloor:
    """A floor cut out of an original level for a module, placed and turned
    as the module: its mesh's name (ballance_level_elements.py writes
    <name>.obj), its groups, and the boxes (x0, x1, y0, y1, z0, z1 in the
    module's frame) that stand in for it without the game's meshes."""

    def __init__(self, name: str, groups: list[str], boxes: list[Box]) -> None:
        self.name, self.groups, self.boxes = name, groups, boxes


MODULE_FLOORS = {
    "P_Modul_37": [
        ModuleFloor("Floor_Modul_37", STONE_FLOOR_GROUPS,
                    [(-16.27, -11.27, -5, 0, -SWITCH_CROSS, -4.75),
                     (-16.27, -11.27, -5, 0, 4.75, SWITCH_CROSS),
                     (-1.27, 3.73, -5, 0, -2.5, 2.5)]),
        ModuleFloor("Floor_Stopper_37", STOPPER_GROUPS,
                    [(-16.27, -11.27, -1, -0.5, -4.75, 4.75)])],
    "P_Modul_25": [
        ModuleFloor("Floor_Modul_25", STONE_FLOOR_GROUPS,
                    [(-15, -10, -5, 0, -2.5, 2.5), (0, 5, -5, 0, -2.5, 2.5)]),
        ModuleFloor("Floor_Stopper_25", STOPPER_GROUPS, [(-10, -9, -1, -0.5, -2.5, 2.5)])],
}
