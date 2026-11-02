"""The modules (P_Modul_*) a route lays on the track, and the floors they
come with."""

from __future__ import annotations

from ..core.obj import Box
from ..game.catalog import RAIL_GROUPS, STONE_FLOOR_GROUPS, STOPPER_GROUPS, WOOD_FLOOR_GROUPS
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
    "Trough": ({"Wood"}, "can't be counted on to swing over into the next trough (on Level_04's, "
                         "a stone ball never does and a paper one only sometimes); a wood ball "
                         "does"),
    "Cradle": ({"Wood", "Stone"}, "can hardly push the wood ball on the rail into the "
                                  "cradle; a wood or stone ball does"),
    "Spiral": ({"Wood", "Paper"}, "can't climb the wood spiral's ramps and rolls back to its "
                                  "foot; a wood or paper ball climbs them"),
    "Modul02": ({"Wood", "Stone"}, "falls short of {kind}'s landing (in Level_09 too); a wood "
                                   "or stone ball makes it"),
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

    def __init__(self, name: str, groups: list[str], boxes: list[Box],
                 visible: bool = True) -> None:
        self.name, self.groups, self.boxes, self.visible = name, groups, boxes, visible


MODULE_FLOORS = {
    # not a module: the fan's housing block a rail ends in (the rail along -Z)
    "FanBlock": [ModuleFloor("Floor_FanBlock", STONE_FLOOR_GROUPS,
                             [(-2.5, 2.5, 0, 5.0, -2.5, 2.5)])],
    # not a module: Level_04's fan tower round its fan (the rail coming in
    # along Z): the shaft, the rail bending up into it, the cap and the
    # hidden plates under it
    "FanTower": [
        ModuleFloor("Floor_FanTower", STONE_FLOOR_GROUPS,
                    [(-3.6, 3.6, 2.41, 12.38, -3.6, -3.05), (-3.6, 3.6, 2.41, 12.38, 3.05, 3.6),
                     (-3.6, -3.05, 2.41, 12.38, -3.05, 3.05),
                     (3.05, 3.6, 2.41, 12.38, -3.05, 3.05)]),
        ModuleFloor("Rail_FanTower", RAIL_GROUPS, [(-1.9, 2.5, -4.7, 21.0, -3.6, -3.0)]),
        ModuleFloor("FanTower_Cap", [], [(-3.5, 3.7, 21.1, 29.6, -3.8, 3.6)]),
        ModuleFloor("FanTower_Hidden", ["Phys_Floors"], [(-3.5, 3.7, 29.0, 29.1, -3.8, 3.6)],
                    visible=False)],
    # not a module: a wide road's end narrowing to a rail (the road's flat
    # middle at the origin, the road along Z, the rail at -Z)
    "RailDock": [ModuleFloor("Floor_RailDock", STONE_FLOOR_GROUPS,
                             [(-5, 5, -5, 0.7, -5, 0)])],
    # not a module: the cradle a rail drops the ball into, with the rail's
    # end and the railing
    "Cradle": [ModuleFloor("Floor_Cradle", STONE_FLOOR_GROUPS,
                           [(-7.5, 7.5, -2.5, 1.8, -7.5, 7.5)]),
               ModuleFloor("Rail_Cradle", RAIL_GROUPS, [(-2.25, 2.25, -1.3, 9.4, -2.5, 3.84)]),
               ModuleFloor("Misc_Cradle", [], [(4.0, 6.0, 1.0, 1.5, -8.0, -3.0)])],
    # not a module: a rail's end whose tubes part (its last straight ring at
    # the origin, the rail coming along X)
    "Rail_Split": [ModuleFloor("Rail_Split", RAIL_GROUPS,
                               [(-10.35, 0, -0.35, 0.35, -11.9, -1.55),
                                (-10.35, 0, -0.35, 0.35, 1.55, 11.9)])],
    # not modules: the arched wood plank, the wavy walkway, the twist (with
    # the floors at its mouths) and the jump (with the area it lands in),
    # floors of their own
    "Modul02": [ModuleFloor("Modul02", WOOD_FLOOR_GROUPS,
                            [(-28.2, 28.2, -11.84, 11.84, -2.5, 2.5)]),
                ModuleFloor("Floor_Modul02", STONE_FLOOR_GROUPS,
                            [(42.49, 57.49, -18.18, -13.18, -7.5, 7.5)])],
    "Modul11": [ModuleFloor("Modul11", WOOD_FLOOR_GROUPS,
                            [(-5.38, 6.62, -3.61, -3.19, -29.33, 29.33)]),
                ModuleFloor("Floor_Modul11", STONE_FLOOR_GROUPS,
                            [(-1.88, 3.12, -8.19, -3.19, -29.33, -24.33),
                             (-1.88, 3.12, -8.19, -3.19, 24.33, 29.33)])],
    "Modul27": [ModuleFloor("Modul27", WOOD_FLOOR_GROUPS, [(-5, 5, -2.78, -0.03, -2.5, 2.5)])],
    "Modul42": [ModuleFloor("Modul42", WOOD_FLOOR_GROUPS,
                            [(-35.16, 35.16, -2.04, 0.41, -2.5, 2.5)])],
    # the long one, along Z
    "Modul42_Long": [ModuleFloor("Modul42_Long", WOOD_FLOOR_GROUPS,
                                 [(-2.5, 2.5, -2.04, 0.41, -65.045, 65.045)])],
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

# not a module: the trough with holes in its bottom, 350 long along X
MODULE_FLOORS["Trough_Holes"] = [ModuleFloor("Trough_Holes", WOOD_FLOOR_GROUPS,
                                             [(-175, 175, -2.9, -2.36, -9.29, 9.29)])]
# not a module: Level_01's wood spiral, from where the road it leaves comes in
# along X (to its right at -Z), its pads, the road's corner, its railings and
# their clamps
MODULE_FLOORS["Spiral"] = [
    ModuleFloor("Spiral", WOOD_FLOOR_GROUPS, [(0, 10, -0.8, 0, -88.41, -5.0)]),
    ModuleFloor("Spiral_Pads", STONE_FLOOR_GROUPS,
                [(x - 5, x + 5, y - 5, y, -z - 5, -z + 5)
                 for x, z, y in [(5.0, 58.94, 19.58), (45.58, 58.94, 31.81),
                                 (45.58, 24.47, 41.6), (17.23, 24.47, 48.94),
                                 (17.23, 83.41, 68.52)]]),
    ModuleFloor("Floor_Spiral_Corner", STONE_FLOOR_GROUPS, [(0, 10, -5.0, 0, -5.0, 5.0)]),
    ModuleFloor("Spiral_Rails", RAIL_GROUPS, [(0, 0.5, 0, 0.5, 5.0, 5.5)]),
    ModuleFloor("Spiral_Clamps", [], [(0, 0.5, -0.2, 0.3, 5.2, 5.7)])]
# not a module: Level_06's logs climbing with fans, from where the ball comes
# in on the lower log's top along -Z, the hidden funnels and wall, and the
# wall's rail and poles
MODULE_FLOORS["LogClimb"] = [
    ModuleFloor("Log_Climb", WOOD_FLOOR_GROUPS,
                [(-8.42, 8.42, -4.53, 0, -96.55, 0), (-8.42, 8.42, 6.47, 11.0, -141.13, 0),
                 (-8.42, 8.42, 16.47, 21.0, -191.13, -100.0),
                 (-8.42, 8.42, 26.47, 31.0, -300.0, -150.0),
                 (-8.42, 8.42, 15.47, 20.0, -333.3, -258.87)]),
    ModuleFloor("Log_Climb_Hidden", ["Phys_Floors"], [(-11.8, 11.8, 31.0, 39.0, -300.7, -299.3)],
                visible=False),
    ModuleFloor("Log_Climb_Rail", RAIL_GROUPS, [(-11.0, 11.0, 34.0, 34.4, -300.4, -299.6)]),
    ModuleFloor("Log_Climb_Poles", [], [(-12.2, 12.2, -10.0, 46.0, -300.6, -299.4)])]
# not modules: troughs and logs of 1 to 8 segments of 50, along X, each one
# mesh round its middle
for _n in range(1, 9):
    MODULE_FLOORS[f"Trough_{_n}"] = [ModuleFloor(
        f"Trough_{_n}", WOOD_FLOOR_GROUPS, [(-25 * _n, 25 * _n, -2.9, -2.36, -9.29, 9.29)])]
    MODULE_FLOORS[f"Log_{_n}"] = [ModuleFloor(
        f"Log_{_n}", WOOD_FLOOR_GROUPS, [(-25 * _n, 25 * _n, -2.26, 2.26, -8.42, 8.42)])]
