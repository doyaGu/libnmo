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

# --- Rails -----------------------------------------------------------------

# A rail may end in the air over a lower area, the ball falling into it:
# Level_01's drop it 12 to 14.4 (A02_Rail_02, A03_Rail_01), their ends 1.2 to
# 4.5 in over the area's inside.  The builder hangs a rail's end two cells
# over the area and lets it fall at most 15.
RAIL_OVERHANG, RAIL_FALL = 5.0, 15.0

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

# P_Modul_34, a stone on a box (Level_01's P_Modul_34_01 and its plaza): a
# walkway 5 wide at a narrow road's edge height runs from a stone trafo's
# platform over the module, which stands in a gap in it 24.6 along, to a wide
# road leaving the plaza 55 along, 2.1 higher and climbing 1.3 every 5.  On
# either side of the walkway lies a pit 45 wide, ramps down into it from the
# rim, and the channel under the gap.  Only a stone ball, pushing along the
# channel through the middle of the box, shoves it out from under the stone,
# which drops flush into the gap.  The ball's path, (along, to the right,
# frames to hold): the trafo; back up the walkway and down the right ramp;
# the push, and on to send the box into the far corner; back up the other
# ramp and over the stone.
STONEBOX_LENGTH, STONEBOX_MODULE, STONEBOX_TRAFO = 55.0, 24.6, 2.5
STONEBOX_RISE = 2.1
STONEBOX_PATH = [(2.5, 0, 0), (6.75, 0, 0), (15.75, 8.75, 0), (17.75, 9.75, 0),
                 (19.75, 9.75, 0), (20.75, 10.75, 0), (22.75, 10.75, 0), (23.75, 11.75, 0),
                 (23.75, 14.75, 0), (24.25, 15.25, 0), (32.25, 16.25, 25), (32.25, -5.75, 0),
                 (41.25, -15.75, 20), (42.75, -17.25, 0), (45.75, -14.25, 0),
                 (45.75, -9.25, 0), (43.75, -7.25, 0), (33.75, -7.25, 0), (32.75, -8.25, 0),
                 (26.75, -8.25, 0), (25.75, -9.25, 0), (15.75, -9.25, 0), (13.75, -7.25, 0),
                 (13.75, -3.25, 0), (16.75, 0, 0), (51.75, 0, 0)]

# P_Modul_01, a pair of pushers (Level_01's and their plaza): a corridor 5
# wide at a narrow road's edge height, 52.5 long, its last 5 narrowing into
# a narrow road's V, crosses a plaza; a ring
# road round it meets its start, and the pushers stand 28.15 and 38.15 along
# it, 2.5 to its right.  Each pusher is a frame on a key that slides in a
# groove across the corridor; its crossbar stands in the corridor until the
# ball pushes the frame's target, south of it on the low ground, as far as
# it goes (2.65).  The ball's path, (along, to the right, frames to hold):
# off the corridor down to the low ground, each target pushed, back by the
# ring road's ramp, and along the corridor past the crossbars, close to its
# right edge.  The ball rams each target twice, at speed, from a stop 9 back
# from it: a slow push moves a frame too little, and one ram 1.7 to 2.2.
PUSHERS_LENGTH, PUSHERS_ASIDE = 52.5, 2.5
PUSHERS_MODULES = [28.15, 38.15]
PUSHERS_PATH = [(6.9, 0, 0), (20.9, 0, 0), (23.9, 6.8, 0), (25.9, 12.8, 0), (27.9, 13.8, 0),
                (27.5, 14.3, 10), (27.5, 5, 0), (27.5, 14.3, 10), (27.5, 5, 0), (27.9, 6.8, 0),
                (32.9, 11.8, 0), (33.9, 11.8, 0), (34.9, 12.8, 0), (36.9, 12.8, 0),
                (37.9, 13.8, 0), (37.5, 14.3, 10), (37.5, 5, 0), (37.5, 14.3, 10), (37.5, 5, 0),
                (32.9, 10.8, 0), (29.9, 11.8, 0), (17.9, 23.8, 0), (5.9, 23.8, 0), (3.9, 21.8, 0),
                (3.9, 2.8, 0), (5.9, 0.8, 0), (25.9, 0.8, 0), (26.9, 1.8, 0), (28.9, 1.8, 0),
                (29.9, 0.8, 0), (35.9, 0.8, 0), (36.9, 1.8, 0), (38.9, 1.8, 0), (39.9, 0.8, 0)]

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
