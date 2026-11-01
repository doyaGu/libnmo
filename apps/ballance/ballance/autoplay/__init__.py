"""Playing a level by itself: a path for the ball found on the level's
ground, as points for `nmo behavior run --steer`.

The ground is sampled on a grid (terrain); a path is searched over it as the
ball can roll, climb and fall (search); a plan names what the ball must do
in turn, the legs found by search or given as points (plan); and the paths
become the autopilot's points (steer).
"""
