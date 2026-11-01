"""The dimensions measured on the original levels that a generated level
follows (docs/ballance-level-format.md).  The floors' own grid (cell, rise,
wall) is the kit's (ballance.kit.floors)."""

from __future__ import annotations

# --- The start, the checkpoints, the balloon -------------------------------

# The start and the checkpoints are 1.26 below the edges of the roads that
# meet their floors.  A narrow road meets those floors through a funnel 5
# long, from the gap in their rims; at the end, a funnel flattens it under the
# balloon's planks.  PR_01 is 3.65 above the start and the next reset point
# 3.33 above its checkpoint.
ROAD_ABOVE_MARKER = 1.26
FUNNEL = 5.0
START_OPENING, START_GAP = 7.39, 1.30
CHECKPOINT_OPENING, CHECKPOINT_GAP = 4.92, 0.87
FIRST_RESET_ABOVE, RESET_ABOVE = 3.65, 3.33
# The balloon's planks reach 22.08 along its +X, back to the road's end, which
# is 0.01 higher than the balloon.
BALLOON_REACH = 22.08
BALLOON_BELOW_ROAD = 0.01

# --- Elements --------------------------------------------------------------

# How far above the floor under it each element stands, as in the originals:
# their meshes rest on it.  A ball changer stands 0.1 above its pad.
ELEMENT_LIFT = {"P_Extra_Point": 2.5, "P_Extra_Life": 2.56, "P_Box": 2.5,
                "P_Ball_Paper": 2.0, "P_Ball_Stone": 2.0, "P_Ball_Wood": 2.0, "P_Dome": 0.0,
                "P_Modul_18": -0.35}
TRAFO_LIFT = 0.1

# --- Modules ---------------------------------------------------------------

# A fan (P_Modul_18) stands on the grille of its housing, 0.35 below the
# floor around it; its wind reaches 21.4 up, so it lifts a paper ball onto
# floors up to 16 higher (Level_03's lift it 13.5).
FAN_BELOW = 0.35
FAN_REACH = 16.0
# The housing's stand-in: its rim flush with the floor, 3.4 deep.
FAN_PIT = 3.4

# P_Modul_25's floors run along its -X: a flat block 5 long, the gap 10 long
# its plank falls across, another block 5 long holding the stopper the plank
# lands on.
DRAWBRIDGE_FLOORS, DRAWBRIDGE_HINGE = 20.0, 5.0

# P_Modul_41 is a slab 5 square on an axle across the track; the originals
# lay 2 to 8 in a row, 6 apart, with a gap of 1 on either side of each.
SLAB, SLAB_GAP = 5.0, 1.0

# P_Modul_26 is a sack on a rope, hung from 15 above it; the levels hang them
# 10 to 16 apart over flat roads at a narrow road's edge height, 2.68 below
# the module, turned every other one the other way.
SACK_STEP, SACK_ABOVE = 15.0, 2.68

# P_Modul_37, the switch (Level_05's P_Modul_37_02): its plank, hinged at a
# target block 5 long, falls across a gap 10 long onto the middle of a cross
# piece whose two blocks funnel down to narrow roads 9.75 from its middle, so
# that the track crossing it runs on over the plank.  The target block lies
# 12.5 to 17.5 to the side of the track, the module 13.77; a stone ball
# pushing the target from the far side tips the plank (about 300 frames).
SWITCH_CROSS, SWITCH_MODULE, SWITCH_TARGET = 9.75, 13.77, 17.5
# The generated switch: a square area, a spur 32.5 aside, frames to push.
SWITCH_AREA, SWITCH_SPUR, SWITCH_HOLD = 15.0, 32.5, 500

# --- Scenery ---------------------------------------------------------------

# A pillar under a floor stands 2.5 in from its corner, its top 5 below the
# floor's edge, where the floor's walls end.  Lantern towers stand in pairs
# beside the track: (along the track, to its right, top of the pillar above
# the marker).  The start's are Level_01's, moved back beside the road ahead
# of it; the checkpoint's are the originals' usual pair 20 ahead.  Around the
# balloon, Level_01's towers and striped poles, along the balloon's +X (back
# toward the road).  A tower that would stand on a floor is left out.
PILLAR_INSET = 2.5
START_TOWERS = [(17.4, 13.0, -2.42), (17.4, -13.0, -2.42)]
CHECKPOINT_TOWERS = [(20.0, 12.5, -4.92), (20.0, -12.5, -4.92)]
BALLOON_TOWERS = [(-50.0, 32.0, -10.0), (-50.0, -32.0, -10.0),
                  (36.0, 26.0, -14.1), (36.0, -26.0, -14.1)]
BALLOON_STICKS = [(-56.0, 45.5, 3.7), (-56.0, -45.5, 3.7), (3.0, 30.5, 3.7),
                  (3.0, -30.5, 3.7), (52.0, 15.5, 3.7), (52.0, -15.5, 3.7)]
TOWER_HALF, STICK_HALF = 2.5, 1.2

# --- Below the level -------------------------------------------------------

# The fall zone reaches this far beyond the floors, from 25 to 60 below them;
# the cloud plane is as far below as the originals'.
FALL_MARGIN = 60.0
FALL_TOP, FALL_BOTTOM = 25.0, 60.0
SKY_BELOW = 180.0
