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
# The balls are 4 across, about.
BALL_RADIUS = 2.0
# Boxes piled up stand 5.0 to 5.6 apart in Level_08's and Level_12's piles
# (two to four high), a box being 5 tall: they settle onto one another.
BOX_PILE = 5.4

# --- Rails -----------------------------------------------------------------

# A rail may end in the air over a lower area, the ball falling into it:
# Level_01's drop it 12 to 14.4 (A02_Rail_02, A03_Rail_01), their ends 1.2 to
# 4.5 in over the area's inside.  The builder hangs a rail's end two cells
# over the area and lets it fall at most 15.
RAIL_OVERHANG, RAIL_FALL = 5.0, 15.0
# Level_12's first sector zigzags on rails (A01_Rail_02 to _05): its lanes
# rise over a hump 12.9 high or dip as low, a raised cosine 116 long with
# rings every 5 along it, and turn back through hairpins 16 across.  The
# builder's hairpin is two of the bends with a straight of 1.3 between them,
# its lanes 17.5 apart, on the grid.
RAIL_RING_STEP, HAIRPIN_GAP = 5.0, 1.3
# A sack (P_Modul_26) hangs under each of its humps' crests, its rope from
# the rail's middle line 15.16 above the module, its X across the lane (over
# a road the levels turn it along the track); an extra point floats 2.98
# above the rail's middle there.
SACK_HANG, CREST_POINT = 15.16, 2.98
# Logs (Level_12's A08_Halfpipe_*, "halfpipes" in the levels' names): half
# logs 16.84 wide, their tops 2.26 above their axes, built of 50-long
# segments with a flat face at either end.  Level_12 lays them crossing at
# right angles, each about 10.3 below the last: the ball rolls along one,
# off its side where the next crosses under it, and on along that one.
# The builder crosses the next 25 from a log's far end, 25 from its own near
# end; the ball lands on the first one 5 past the rail's end it falls from.
LOG_SEGMENT, LOG_TOP, LOG_CROSS = 50.0, 2.26, 25.0
# Level_01's wood spiral up to its last floor (A04_Floor_Wood_01 and _02 on
# A04_Floor_02's pads): a wide road's last 10 turns the ball right onto a
# wood ramp 10 wide that rises 0.4 a unit from the road's middle, round the
# stone pads at its corners twice (inside the first round the second time),
# rails for its railings.  It comes out 52.82 on from where the road's last
# 10 starts and 83.41 to the right, heading as the road came in, its end
# level with the rim of a wide road whose middle is 80.05 above the first's.
# From that start: the middles of the road's last 10 and of the pads (along,
# to the right), and where it comes out (along, to the right, up).
SPIRAL_PATH = [(5.0, 0.0), (5.0, 58.94), (45.58, 58.94), (45.58, 24.47), (17.23, 24.47),
               (17.23, 83.41)]
SPIRAL_OUT = (52.82, 83.41, 80.05)
# Level_06's logs climbing with fans (A_02_Wood_04 to _14): a log the ball
# comes in along, under three more that start 0, 50 and 150 on and whose
# tops are 11, 21 and 31 up (12.21 wide, on 16.84 wide undersides), each but
# the top one with a fan set in it whose wind blows a paper ball up through
# a hole in the log above (a hidden funnel under the hole leads it in).  On
# the top log a ball changer turns the ball to wood 19 back from where the
# last fan lifts it; the log ends in a wall 300 on, and before the wall the
# wood ball drops through a hole, over a fan too weak for it, onto a log 20
# up that goes on 33.3 past the wall.  From where the ball comes in on the
# lower log's top: each fan lifting the ball (along, up) and the top of the
# log it lifts it onto, the ball changer, the hole (along, its fan up), and
# the last log's end and top.
CLIMB_FANS = [(77.66, -0.845), (127.70, 10.261), (177.69, 20.265)]
CLIMB_TOPS = [11.0, 21.0, 31.0]
CLIMB_TRAFO = (158.6, 31.057)
CLIMB_DROP = (272.30, 19.219)
# The frames the autopilot holds the ball over the hole.
CLIMB_HOLD = 30
CLIMB_LENGTH, CLIMB_END = 333.3, 20.0
# Logs and troughs are exported 1 to 8 segments long, each length one mesh.
LOG_MOST = 8
# Troughs (Level_04's A01_Halfpipe_01 to _04): wooden U troughs 18.58 wide,
# open at both ends, their bottoms 2.36 below their axes and their rims 2.86
# above, built of 50-long segments.  Level_04 lays them side by side, each
# 19.6 aside of the last at the same height and overlapping its last 50;
# the ball swings up the far wall and over the rim into the next (a stone
# ball never gets over, a paper one only sometimes).  Its way over, from
# where the next trough starts (along, aside toward the next, rush): back
# 27 in the middle, 18 on up the far wall 8 out, rushing to the next one's
# middle 48 on (worked out on Level_04).  The ball drops into the first 10
# past its near end, and leaves the last over its open end, onto a log
# whose top is level with its bottom 0.5 on (A01_Halfpipe_05) or off it.
TROUGH_SEGMENT, TROUGH_BOTTOM, TROUGH_ASIDE, TROUGH_LAND = 50.0, 2.36, 19.6, 10.0
TROUGH_HOP = [(-27.0, 0.0, False), (18.0, -8.0, False), (48.0, TROUGH_ASIDE, True)]
TROUGH_LOG_GAP = 0.5
# Level_04's A01_Halfpipe_04 is a trough 350 long with five holes 11.8 long
# in its bottom, every 50 from 75 to 275 along it; the ball rides past each
# up a wall, 6.3 out, to its right first and then by turns (as the extra
# points Level_04 sets there have it), heading for each from the trough's
# middle 48 along it.
HOLED_TROUGH, TROUGH_HOLES, TROUGH_DODGE = 350.0, [75.0, 125.0, 175.0, 225.0, 275.0], 6.3
TROUGH_HOLES_FROM = 48.0
# A rail ends in a fan's housing block (Level_05's A03_Floor_Modul18, as
# Level_13 sets many): a 5 by 5 block 5 deep, its top 5 above its bottom,
# sloping into the housing whose grille the fan stands on 4.66 up; the rail's
# last ring stands at the middle of one of its sides, 0.36 below the top,
# heading into it.  The fan lifts a paper ball onto the next piece, which
# starts at the far side.  (ring, fan, top, above its bottom)
FAN_BLOCK_RING, FAN_BLOCK_FAN, FAN_BLOCK_TOP = 4.64, 4.66, 5.0
# Level_04's fan towers (P_Modul_18_05 with A04_Rail_03): a rail comes in
# low and bends straight up round a fan that stands on a pillar, its tubes
# going on up a shaft 6.1 wide to a cap 21 above the fan.  The fan blows a
# paper ball up between them, a hidden plate under the cap throws it back
# the way the rail came, and it lands in the innermost of three square
# channels round the shaft, which wind down round it, railed on the side the
# ball is thrown to, to a flat way out 17.9 to the rail's left, 0.11 above
# the fan.  The rail comes in level under the channels (the generated one
# for FAN_TOWER_RUN before the bend).  In the fan's frame, the rail coming in
# along its Z: the rail's first ring and the pillar's top; the height the
# ball is lifted to, and the way down the channels (x, z) to the way out's
# middle (x, top, z).  The way down leaves the shaft's top to the left, off
# the rail: steered straight back, the ball may stick against the plate
# under the cap (in two of three runs).
FAN_TOWER_RING, FAN_TOWER_RUN = (0.283, -4.462, -3.655), 15.0
FAN_TOWER_PILLAR, FAN_TOWER_LIFT = -1.62, 17.07
FAN_TOWER_PATH = [(-4.6, -2.3), (-4.6, 6.7), (-0.6, 10.7), (-7.6, 10.7), (-10.6, 7.7),
                  (-10.6, -11.3), (15.4, -11.3), (15.4, 12.7), (12.4, 15.7), (-13.6, 15.7),
                  (-15.6, 13.7), (-15.6, 0.5)]
FAN_TOWER_EXIT = (-17.9, 0.11, 0.48)
# How far across the rail, and how many frames, the autopilot rocks the
# ball in the bend (the fan blows once the ball's turning bounding box
# reaches its wind box, 1.14 above it, which a ball resting there is just
# short of).
FAN_TOWER_ROCK, FAN_TOWER_HOLD = 1.5, 40
# Railings (ballance.kit.railings): Level_01's round a floor's corner
# (A01_Rail_04) has its posts 4.76 along the walls from the corner, as a
# railing along a road has them 0.24 in from its ends; Level_05's beside a
# rail across a gap (A01_Rail_01) have them 5.64 back on the roads at either
# end.
RAILING_ARM, RAILING_INSET, RAILING_BRIDGE = 4.76, 0.24, 5.64
# A rail meets a wide road's end as Level_01's A01_Rail_07 meets A01_Floor_03:
# through a piece 5 long whose slopes go on as the road's while its flat
# middle sinks to a narrow road's V, 0.7 deep at the rail; the tubes' middles
# are 0.58 below the road's flat middle there.
RAIL_DOCK_LENGTH, RAIL_DOCK_BELOW = 5.0, 0.58
# Level_01's A01_Rail_09 ends in a cradle: a 15 by 15 block whose top is
# 2.5 above its middle, with a 5 by 5 pocket there.  The rail comes in high
# along its Z (its last straight ring at (-0.05, 9.09, 3.84) of the block,
# the rail heading -Z), bends straight down to the pocket's edge, and two
# short tubes across the pocket's bottom hold the ball.  A narrow road's V
# leads out of the pocket to the block's -X side, 7.5 from its middle,
# turning the track to the right; a railing stands on the block's far side.
CRADLE_RING, CRADLE_EXIT, CRADLE_TOP = (-0.05, 9.0935, 3.838), 7.5, 2.5
# A wood ball lies on the rail 6.43 before its last straight ring, 2.08
# above its middle (P_Ball_Wood_03): the ball rolling down the rail pushes it
# on, it drops into the pocket first and fills it, and the ball lands on it
# and rolls straight out, where from the pocket's bottom it would have to
# climb.  (back, above)
CRADLE_BALL = (6.43, 2.08)
# Level_01's A01_Rail_02_01 ends otherwise: past its last straight ring its
# tubes bend apart, 20 apart 10.35 on, and the ball drops between them
# onto the walled plaza 13.3 below.

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
# 12.5 to 17.5 to the side of the track, the module 13.77, and a flat block
# 5 long goes on from it to 22.5, where a rail coming down from a node ends,
# its tubes' middles 0.43 above the block: a stone ball pushed off the node
# rolls down onto the target and tips the plank at once (one pushing it
# tips it in about 300 frames; a wood ball does not).  The generated switch
# has its middle 10 on, on the grid, and how long the autopilot waits at
# the gap for the plank.
SWITCH_CROSS, SWITCH_MODULE, SWITCH_LAND, SWITCH_SPAN = 9.75, 13.77, 22.5, 10.0
SWITCH_RAIL_ABOVE, SWITCH_WAIT = 0.43, 400
# A stone ball waiting at a node's side for the ball to push it down a
# branch: how far in from the side it lies; the autopilot stops this far in
# from the side, behind it, then runs into it on to this far short of the
# side, stopping this many frames at each.
PUSHED_BALL_IN, PUSHED_BALL_BACK, PUSHED_BALL_REACH, PUSHED_BALL_HOLD = 5.0, 12.5, 2.5, 40

# P_Modul_34, a stone on a box (Level_01's P_Modul_34_01 and its plaza): a
# walkway 5 wide at a narrow road's edge height runs from a stone trafo's
# platform over the module, which stands in a gap in it 24.6 along, to a wide
# road leaving the plaza 55 along, 2.1 higher and climbing 1.3 every 5.  On
# either side of the walkway lies a pit 45 wide, ramps down into it from the
# rim, and the channel under the gap.  Only a stone ball, pushing along the
# channel through the middle of the box, shoves it out from under the stone,
# which drops flush into the gap.  The ball's path, (along, to the right,
# frames to hold): the trafo; back up the walkway and down the right ramp;
# the push, and on to send the box into the far corner; straight up the
# other ramp to the rim, turning only there, and over the stone.
STONEBOX_LENGTH, STONEBOX_MODULE, STONEBOX_TRAFO = 55.0, 24.6, 2.5
STONEBOX_RISE = 2.1
STONEBOX_PATH = [(2.5, 0, 0), (6.75, 0, 0), (15.75, 8.75, 0), (17.75, 9.75, 0),
                 (19.75, 9.75, 0), (20.75, 10.75, 0), (22.75, 10.75, 0), (23.75, 11.75, 0),
                 (23.75, 14.75, 0), (24.25, 15.25, 0), (32.25, 16.25, 25), (32.25, -5.75, 0),
                 (41.25, -15.75, 20), (42.75, -17.25, 0), (45.75, -14.25, 0),
                 (45.75, -9.25, 0), (43.75, -7.25, 0), (33.75, -7.25, 0), (32.75, -8.25, 0),
                 (26.75, -8.25, 0), (25.75, -9.25, 0), (11.75, -9.25, 0), (9.75, -7.25, 0),
                 (9.75, -3.25, 0), (12.75, 0, 0), (51.75, 0, 0)]
# Level_10's maze of them (A01_Floor_04 and A01_Rail_02 in P_Modul_34_01's
# frame): three stand 15.4 apart along X over gaps 5.4 long in a flat road,
# its top level with the modules; a road 5 lower winds under the gaps, up
# under the first, down under the second and up under the third, each box in
# its way.  A stone ball going round it pushes each box out from under its
# stone, which drops in behind it flush with the road above, and from the
# lower road's end a rail climbs 5 back to the flat road's start, which the
# ball then crosses on the stones (a wood ball moves a box 1.5 and stops).
# The track comes in at the lower road's start, heading along Z, and leaves
# at the flat road's end, heading along X, where the builder's cut ends on
# the grid.  The ball's way round, (X, Z) in the frame: up the legs and along
# the bars between them, through the middles of the boxes (pushing the
# first two a unit further at their legs' ends, or one may stay there, out
# of its hole, and be pushed on into the next leg; the last leg ends where
# the rail goes back, which the ball would overrun), and round the rail's
# bends to the flat road.
MAZE_STEP, MAZE_BELOW = 15.4, 5.0
MAZE_ENTRY, MAZE_EXIT = (-7.67, -17.5), 44.83
MAZE_POINT = (15.68, 0.0)  # its extra point, between the second gap and the third
MAZE_PATH = [(-7.67, -12.5), (-7.67, 16.0), (7.66, 15.0), (7.66, -16.0), (23.1, -15.0),
             (23.1, 25.0), (15.6, 25.0), (-30.3, 25.0), (-36.0, 22.6), (-38.4, 16.9),
             (-38.4, 8.1), (-36.0, 2.4), (-30.3, 0.0), (-20.4, 0.0)]

# Level_05's octagon node (A01_Floor_01 round (68.9, 0)): a square 15 across,
# its corners cut off 5 by 5, its inside a narrow road's V bottom down from
# its rim; each side's straight middle is 5, a narrow road's V opening onto
# it, and three are open, a T: the track leaves by one, a branch by another.
OCTAGON_HALF = 7.5
# Level_05's diamond (A01_Floor_03 round (286.4, -96.91)): a square 15 a
# side turned 45 degrees, its inside a V bottom down from its rim, landed in
# from a rail's end 5 or so above, 1.22 in over its shut tip, 10.6 behind its
# middle; it is left from its other tip, cut off where it narrows to 5,
# DIAMOND_EXIT ahead of its middle, through a funnel onto a narrow road
# DIAMOND_OUT on from the rail's end.
DIAMOND_EXIT, DIAMOND_OUT = 8.12, 17.5

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
# right edge.  The ball rams each target four times, at speed, from a stop 9
# back from it: a slow push moves a frame too little, and one ram 1.7 to 2.2;
# rammed twice, the first crossbar still stops the ball for 4 seconds.
PUSHERS_LENGTH, PUSHERS_ASIDE = 52.5, 2.5
PUSHERS_MODULES = [28.15, 38.15]
PUSHERS_PATH = [(6.9, 0, 0), (20.9, 0, 0), (23.9, 6.8, 0), (25.9, 12.8, 0), (27.9, 13.8, 0),
                (27.5, 14.3, 10), (27.5, 5, 0), (27.5, 14.3, 10), (27.5, 5, 0),
                (27.5, 14.3, 10), (27.5, 5, 0), (27.5, 14.3, 10), (27.5, 5, 0), (27.9, 6.8, 0),
                (32.9, 11.8, 0), (33.9, 11.8, 0), (34.9, 12.8, 0), (36.9, 12.8, 0),
                (37.9, 13.8, 0), (37.5, 14.3, 10), (37.5, 5, 0), (37.5, 14.3, 10), (37.5, 5, 0),
                (37.5, 14.3, 10), (37.5, 5, 0), (37.5, 14.3, 10), (37.5, 5, 0),
                (32.9, 10.8, 0), (29.9, 11.8, 0), (17.9, 23.8, 0), (5.9, 23.8, 0), (3.9, 21.8, 0),
                (3.9, 2.8, 0), (5.9, 0.8, 0), (25.9, 0.8, 0), (26.9, 1.8, 0), (28.9, 1.8, 0),
                (29.9, 0.8, 0), (35.9, 0.8, 0), (36.9, 1.8, 0), (38.9, 1.8, 0), (39.9, 0.8, 0)]

# P_Modul_29 and P_Modul_30 bridge a gap that two narrow roads' V's run to
# (Level_03's): nine plates hung on hinges like a rope bridge, over a gap
# 15.33 long, the module 6.87 from its near end and 0.17 below the roads'
# edges; a seesaw over a gap 14.8 long, in its middle, 0.5 below the edges,
# its plank resting on a stopper lip at either end.  The ball crosses both
# along the module's +X; a stone ball falls through the plates, in Ballanced
# too.  (gap, module along it, module below the edges)
BRIDGES = {"P_Modul_29": (15.33, 6.87, 0.17), "P_Modul_30": (14.8, 7.4, 0.5)}

# P_Modul_17, a rotating arm, with the drawbridge it tips (Level_03's
# P_Modul_17_01 and P_Modul_25_05): a road runs 33.33 to and across the
# drawbridge's gap, whose plank stands on the far side; the ball can't push
# it from this side.  Beside the road a loop of road climbs to an upper road
# where the arm, a bar 23.2 long on a pivot block 11.21 to the left, 24.61
# along and 5.43 above the road's edges, reaches over both roads.  The ball
# pushes the arm's paddle along the upper road; the arm turns, and its other
# paddle knocks the plank down across the gap (a paper ball can't turn the
# arm; wood and stone do).  The drawbridge stands 23.33
# along.  The ball's path, (along, to the right, frames to hold): up the
# loop, along the upper road past the paddle to its end, back, and over the
# plank.
ARMS_LENGTH, ARMS_MODULE, ARMS_ABOVE, ARMS_BRIDGE = 33.33, (24.61, -11.21), 5.43, 23.33
ARMS_PATH = [(5, 0, 0), (5, -20, 0), (20, -20, 0), (28, -20, 1), (5, -20, 0), (5, 0, 0),
             (20, 0, 0)]

# P_Modul_19, the flaps (Level_09's P_Modul_19_01): an axle 20 long across
# two roads 15 apart, a flap 6.08 long at either end, square to each other.
# Each road has a gap the flap over it bridges, falling onto a stopper lip at
# the gap's far edge.  On the track's road (21.65 from where its flat road
# begins to where it is a narrow road's V again) the flap stands up at the
# gap's near edge, 10 on; on the other road, to the left, it lies across its
# gap with a stone ball on it.  The ball pushes the upright flap over and
# crosses; the turn throws the stone ball off.  The module stands 7.53 on
# and 5 to the left of the track, the stone ball 8.7 on, 14.85 to the left,
# 3.13 above the edges.  (along, right)
FLAPS_LENGTH, FLAPS_MODULE = 21.65, (7.53, -5.0)
FLAPS_COUNTERWEIGHT = (8.7, -14.85, 3.13)

# P_Modul_03, the lift (Level_07's and Level_10's P_Modul_03_01): a floor in
# a cage of seven walls and a gate, hung from a spring.  The track's narrow
# road runs through a funnel up to the gate, the module 15.87 on from where
# the funnel begins (the road's edges 10.76 below it); the platform the lift
# brings the ball up to is on the far side, its top 6.884 above the module,
# its flat middle running on to 35.94 beyond it.  The floor and the walls are
# too heavy for the spring: it rises only once the ball has pushed the walls
# off, rolling out 11 toward each and back, and the ball rides it up to 7
# above the module.  (along, right) from the module
LIFT_ENTRY, LIFT_BELOW, LIFT_TOP, LIFT_EXIT, LIFT_RIDE = 15.87, 10.76, 6.884, 35.94, 7.0
LIFT_WALLS = [(-7.78, 7.78), (0.0, 11.0), (7.78, 7.78), (11.0, 0.0), (7.78, -7.78),
              (0.0, -11.0), (-7.78, -7.78)]

# Modul27, the arched wood plank (Level_03's A04_Modul_27; 19 in the
# originals, never two in a row): 10 long and 5 wide, without walls, over a
# gap 10 long between flat floors at a narrow road's edge height, its ends
# flush with them 1.25 below its middle, 1.22 under its top.
PLANK_LENGTH, PLANK_BELOW = 10.0, 1.25

# Modul42, the wavy wood walkway (Level_08's A02_Modul42, as long in
# Level_07 and Level_11): 70.32 long and 5 wide, without walls, its top
# waving from 1.04 below its middle to 2.04 above every 20, its ends 0.41
# above, flush with flat floors at a narrow road's edge height, over nothing.
WALKWAY_LENGTH, WALKWAY_ABOVE = 70.32, 0.41
# Level_11's A01_Modul42_01 is the same, 130.09 long (six and a half waves
# where Level_08's has three and a half).
WALKWAY_LONG = 130.09

# Modul11, the wood twist (Level_07's A01_Modul11, the same in Level_09 and
# Level_11): a chute 58.66 long that turns the ball once round, its mouths
# lying on flat floors 5 wide which end 24.33 either side of it, over
# nothing between; the floors' tops are 3.19 below the module, and their
# middle line 0.62 to its X side.  (half length, aside, below)  The chute's
# bottom stays level with the floors, 2.2 to the module's -X side for its
# first half and 3.5 to its X side for its second, and the ball follows it:
# rolling straight along it, it falls out, in Level_09 too.  (X, Z)
TWIST_HALF, TWIST_ASIDE, TWIST_BELOW = 29.33, 0.62, 3.19
TWIST_PATH = [(-2.2, -24.0), (-2.2, -8.0), (3.4, 6.0), (3.5, 24.0)]

# Modul02, the wood jump (Level_09's A05_Modul02, the same in Level_05,
# Level_10 and Level_11): a slope 56.4 long and 5 wide, without walls, from a
# flat floor at a narrow road's edge height 11.84 above its middle, down to
# 10.84 below and up again to 9.03 below at its lip; the ball flies 14.29
# from there to a 15 by 15 area whose rim is 25.02 below the floor it left,
# 42.49 to 57.49 from the middle, and leaves it along a flat road.
JUMP_HALF, JUMP_ABOVE, JUMP_DOWN = 28.2, 11.84, 25.02
JUMP_AREA = (42.49, 57.49)

# P_Modul_08, the swing (Level_09's P_Modul_08_01): a seat hung from a frame
# over a gap 15 long in a flat road, pushed across the road every half
# second.  The road's flat top is 1.5 above the module and runs 12.5 either
# side of it; Level_09 reaches it from a trafo's pad, here a funnel.
SWING_HALF, SWING_ABOVE = 12.5, 1.5
# The seat swings along the road, a little under 5 either way.  The ball
# crosses on it from the near end of its swing: it waits 14 before the
# module until the seat has been within 0.8 of 4.3 beyond it and swung back
# to within 1 of 1 beyond it, then rolls on, reaching the gap as the seat
# does.  (along, reach)
SWING_SEAT, SWING_WAIT, SWING_FAR, SWING_GO = "P_Modul_08_Schaukel", 14.0, (4.3, 0.8), (1.0, 1.0)

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

# The fall zones lie 25 to 55 below the floors above them, square by square
# of 100, their tops rounded down to 30 so that squares join into a few (the
# originals' are 20 to 120 deep, a few to a level, each under a part of it,
# from 20 to 60 below its floors); the last catches the ball 60 beyond them.
# The cloud plane is as far below as the originals'.
FALL_MARGIN = 60.0
FALL_TOP, FALL_BOTTOM = 25.0, 60.0
FALL_TILE, FALL_STEP = 100.0, 30.0

# The ball, 4 across, rolls through this much room above a floor's rim;
# what stands more than CLASH_OVERLAP into that room is in its way.
# A sloping rail is looked at CLASH_RAIL_STEP at a time, a sloping road a
# cell at a time.
CLASH_HEADROOM, CLASH_OVERLAP, CLASH_RAIL_STEP = 4.5, 0.25, 5.0
# On a rail the ball's top is 3.4 above the tubes' middles; Level_05's
# A01_Rail_01 runs a rail under another's tubes 4.0 above its own.
CLASH_RAIL_ROOM = 3.6
SKY_BELOW = 180.0
