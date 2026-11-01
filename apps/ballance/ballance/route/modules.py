"""The modules (P_Modul_*) a route lays on the track, and the floors they
come with."""

from __future__ import annotations

from ..core.obj import Box
from ..game.catalog import STONE_FLOOR_GROUPS, STOPPER_GROUPS
from ..game.measures import (DRAWBRIDGE_FLOORS, DRAWBRIDGE_HINGE, SACK_STEP, SLAB, SLAB_GAP,
                             STONEBOX_LENGTH, STONEBOX_MODULE, SWITCH_CROSS)

# The modules that stand on the track, and how many in a row.  Each but the
# switch, the stone on a box, the pushers, the rotating arm, the flaps, the
# lift and the swing (which come with floors of their own) and the bridges (plates and
# seesaw, over a gap narrow roads run to) stands between funnels that lead a
# narrow road onto it at the road's edge height and off it again.
MODULES = {"P_Modul_01": (2, 2), "P_Modul_03": (1, 1), "P_Modul_08": (1, 1),
           "P_Modul_17": (1, 1), "P_Modul_19": (1, 1), "P_Modul_25": (1, 1),
           "P_Modul_26": (1, 4), "P_Modul_29": (1, 1), "P_Modul_30": (1, 1),
           "P_Modul_34": (1, 1), "P_Modul_37": (1, 1), "P_Modul_41": (2, 8)}

# The modules only some balls get past: (those balls, what happens with
# another).
NEEDS_BALL = {
    "P_Modul_41": ({"Paper"}, "tips {kind}'s slabs and falls; only a paper ball crosses them"),
    "P_Modul_37": ({"Stone"}, "does not tip {kind}'s plank; a stone ball does"),
    "P_Modul_29": ({"Paper", "Wood"}, "falls through {kind}'s plates (in Ballanced too); "
                                      "a paper or wood ball crosses them"),
    "P_Modul_17": ({"Wood", "Stone"}, "can't turn {kind}'s arm; a wood or stone ball does"),
    "P_Modul_03": ({"Wood"}, "can't ride {kind} up: stone sinks it and falls off, paper "
                             "can't push its walls off (in Level_12 too); a wood ball does"),
    "P_Modul_08": ({"Wood", "Paper"}, "falls off {kind}'s seat (in Level_09 too); a wood or "
                                      "paper ball rides it across"),
    "P_Modul_19": ({"Wood", "Stone"}, "can't push {kind}'s flap over (in Level_09 too); "
                                      "a wood or stone ball does"),
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
    # in the stand-in, the road either side of the gap
    "P_Modul_08": [
        ModuleFloor("Floor_Modul_08", STONE_FLOOR_GROUPS,
                    [(-12.5, -7.5, -3.5, 1.5, -2.5, 2.5), (7.5, 12.5, -3.5, 1.5, -2.5, 2.5)])],
    # in the stand-in, the platform the lift rises to
    "P_Modul_03": [
        ModuleFloor("Floor_Modul_03", STONE_FLOOR_GROUPS,
                    [(-30.94, -15.94, 1.884, 6.884, -7.5, 7.5),
                     (-35.94, -10.95, 1.884, 6.884, -2.5, 2.5)])],
    # in the stand-ins, the two roads either side of their gaps, and the lips
    "P_Modul_19": [
        ModuleFloor("Floor_Modul_19", STONE_FLOOR_GROUPS,
                    [(-2.47, 7.53, -5, 0, 2.5, 7.5), (-14.12, -9.12, -5, 0, 2.5, 7.5),
                     (3.32, 7.53, -5, 0, -12.5, -7.5), (-8.34, -3.34, -5, 0, -12.5, -7.5)]),
        ModuleFloor("Floor_Stopper_19", STOPPER_GROUPS,
                    [(-9.12, -8.12, -1.03, -0.53, 2.5, 7.5),
                     (2.32, 3.32, -1.03, -0.53, -12.5, -7.5)])],
    # in the stand-ins, the lower road either side of the gap, the loop's
    # column and upper road, and the arm's block
    "P_Modul_17": [
        ModuleFloor("Floor_Modul_17", STONE_FLOOR_GROUPS,
                    [(-13.71, -8.71, -10.43, -5.43, 11.35, 24.61),
                     (-23.74, 13.8, -10.43, -5.43, 17.11, 22.11),
                     (6.29, 11.29, -10.43, -5.43, -5.39, 22.11),
                     (-2.5, 2.5, -9.0, -4.1, -2.5, 2.5)]),
        ModuleFloor("Floor_Modul_17_Far", STONE_FLOOR_GROUPS,
                    [(-13.71, -8.71, -10.43, -5.43, -8.72, 1.28)]),
        ModuleFloor("Floor_Stopper_17", STOPPER_GROUPS,
                    [(-13.71, -8.71, -6.43, -5.93, 10.35, 11.35)])],
    "P_Modul_30": [
        ModuleFloor("Floor_Stopper_30", STOPPER_GROUPS,
                    [(-7.4, -6.4, -0.5, 0, -2.5, 2.5), (6.4, 7.4, -0.5, 0, -2.5, 2.5)])],
    # in the stand-ins, the corridor and the low ground south of it
    "P_Modul_01": [
        ModuleFloor("Floor_Modul_01", STONE_FLOOR_GROUPS,
                    [(0, 5, -5, 0, -24.35, 28.15), (-20, 0, -7.5, -2.5, -22.5, 12.5)])],
    # in the stand-ins, the plaza's walkway either side of the module's gap
    # and the pit's floor; the ramps down from the rim are left out
    "P_Modul_34": [
        ModuleFloor("Floor_Modul_34", STONE_FLOOR_GROUPS,
                    [(-5.2, STONEBOX_MODULE, -5, 0, -2.5, 2.5),
                     (STONEBOX_MODULE - STONEBOX_LENGTH, -10.2, -5, 0, -2.5, 2.5),
                     (STONEBOX_MODULE - STONEBOX_LENGTH, 18.0, -9.5, -5, -22.5, 22.5)])],
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
