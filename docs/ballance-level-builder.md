# Ballance Level Builder

How to build, check and play a Ballance level with the tools in
`apps/ballance`. What a level file holds, and the measurements the builder
follows, are in [ballance-level-format.md](ballance-level-format.md).

The tools are Python scripts. They read and write levels through the `nmo`
CLI (`--nmo`, or `$NMO`, or `nmo` on `PATH`) and take the game's meshes and
textures from a local installation, `<GAME_ROOT>`. Nothing of the game is
kept with libnmo.

## What You Need

- A Ballance installation, `<GAME_ROOT>`, with its `3D Entities`. Its
  `base.cmo` is not needed: levels are played in libnmo's own test base
  (see [The Test Base](#the-test-base)).
- The `nmo` CLI. Playing a level needs it built with libivp
  (`-DNMO_IVP_SOURCE_DIR=<libivp>`); watching it in a window needs
  `-DNMO_VIEWER=ON` and SDL3.
- The element meshes, exported once from the original levels:

```sh
apps/ballance/scripts/ballance_level_elements.py <GAME_ROOT> -o <elements>
```

  These are one mesh of each marker and element kind, the `SkyLayer` plane,
  the scenery, and the floors cut out of the originals: the start's and
  checkpoints' floors, the fan's housing, the modules' blocks and stoppers,
  the plazas of Level_01, Level_10's maze, and Level_05's octagon and
  diamond. `ballance.extract.elements` says what each is and where it is cut
  from. Without them the builder stands boxes in for the markers, elements
  and cut floors, and lays no scenery and no sky.

## From a Route to a Level

```sh
apps/ballance/scripts/ballance_build_level.py <route.json> <out> --elements <elements>
nmo patch apply --project <out>/manifest.json -o <out>/Level_01.NMO
apps/ballance/scripts/ballance_check_level.py <out>/Level_01.NMO
apps/ballance/scripts/ballance_verify_level.py <out> --play 12000 --game <GAME_ROOT>
apps/ballance/scripts/ballance_play_level.py <out>
```

`ballance_build_level.py` lays the route out and writes into `<out>`:

| File | Contents |
|------|----------|
| `manifest.json` | The level as a libnmo project manifest, with the OBJ meshes it names |
| `route_points.json` | The points along the middle of the track, start to balloon, for the autopilot |
| `route_sectors.json` | Each sector's start: the ball the player has there and the index of its first point |
| `route_plan.txt` | Where each piece leaves the track: its point, the floor's height there and the heading |

It prints a warning for each thing that builds but plays wrong (see
[What the Builder Checks](#what-the-builder-checks)). `nmo patch apply`
turns the manifest into the level file. A level is always built for slot 1:
its sky, music and start ball belong to the level number, not to the file.

## Routes

A route is JSON: the start's place and heading, then the track sector by
sector, each a list of pieces laid end to end on the floors' 2.5 grid. The
docstring of `ballance_build_level.py` gives every piece and option with
its measurements; in short:

- **Floors:** narrow and wide roads, coming down or climbing, with railings
  along them; areas left straight on or from a side, with railings round
  their corners and Level_08's stepped blocks on their insides; square
  turns; ball changers on their pads.
- **Nodes and branches:** an area or Level_05's octagon may branch off a
  side. A branch with `"visit": true` is a spur the ball goes down and comes
  back from. A branch with `"to": NAME` comes back by itself to a later area
  of the sector, by road or rail; the autopilot takes it with
  `"take": true`. A branch with `"push": true` is a rail to a named
  switch: a stone ball waits by the side it leaves from, and the ball
  pushes it down onto the switch's target, as Level_05's nodes send theirs.
  Level_05's diamond catches a ball a rail drops.
- **Wood pieces:** Level_03's arched plank, Level_08's and Level_11's
  walkways, Level_07's twist, Level_09's jump and Level_01's spiral.
- **Rails:** straight, coming down, humped or dipped, bent, or turned back
  on themselves through a hairpin. A rail ends on a floor, in a fall into an
  area, a log, a trough or a diamond, in Level_01's cradle, in a fan's
  block, or in Level_04's fan tower, whose three channels wind down round
  its shaft to a way out on the left.
- **Beams:** Level_12's single tubes the ball balances along on top of,
  going on from a rail and back onto one, humped or dipped, and turned back
  onto a beam 8 aside through a small half circle of rail. Level_12's
  zigzag is built of them: lanes 8 apart rising and dipping by turns, an
  extra point over each crest and a sack under it, swinging into a beam that
  crosses the lanes below the crests and above the dips.
- **Logs and troughs:** Level_12's half logs crossing one under another,
  Level_06's logs climbing with fans, and Level_04's troughs, hopped between
  or with holes in their bottoms and an extra point by each of the first four,
  up the wall the ball rides past it on.
- **Modules:** the pushers (`P_Modul_01`), lift (`_03`), swing (`_08`),
  rotating arm (`_17`), flaps (`_19`), drawbridge (`_25`), sacks (`_26`),
  hung plates (`_29`), seesaw (`_30`), stone on a box or Level_10's maze of
  three (`_34`), switch (`_37`, its target off the track, for a pushed stone
  ball) and slabs (`_41`), each laid with the floors its original stands
  on. Fans (`P_Modul_18`) stand in areas or end rails.
- **Elements:** extra points and lives, boxes, domes, loose balls, placed
  along and across a piece, resting on its floor as the originals' do, or
  hung up (`"up"`) or piled (`"pile"`).

Each sector but the last ends in a checkpoint, and the last in the balloon.
The builder stands the lantern towers and poles beside them, pillars under
the areas, and the hidden fall zone under every part of the track.

## What the Builder Checks

A route that cannot be laid stops the build with an error that says why,
such as a fall that lands anywhere but in an area, a diamond, on a log or
in a trough; an octagon left by anything but a narrow road; a rail that
starts or ends anywhere but on a road or an area; a branch to an area that
it does not reach at its side's middle and height; a switch that no branch
pushes a stone ball to; a beam that does not go on from a rail, or that
goes on to anything but a beam, a rail or a fall; a sack or an extra point
over a crest asked of a rail rather than a beam.

The warnings are for levels that build but play wrong:

- a ball that cannot get past a mechanism (`NEEDS_BALL` in
  `ballance.route.modules`): only paper rides a fan or crosses the slabs,
  only stone clears the maze, only wood rides the lift up
  or is sure to hop between troughs, and stone falls through the hung
  plates;
- a ball that reaches the balloon other than wood (every original turns it
  back to wood before the balloon's bridge);
- a block standing in the ball's way through an area;
- pieces in one another's way (`ballance.route.clashes`): anything within
  4.5 above a floor, or 3.6 above a rail's middle, of a piece that does not
  follow it. A rail may pass under another, or a log reach back under the
  rail it is dropped from; sacks (`P_Modul_26`) swing into the way on
  purpose.

## Verifying a Level

`ballance_verify_level.py SOURCE` checks the floors of a built folder or a
level file (`ballance.verify.geometry`): walls left open at their ends,
holes in floor tops, and tops of two objects lying on one another. Some are
meant: the cradle's pocket is a hole, the fan tower's way out leaves two
wall ends open against its block's end wall, and Level_01 has 21 open
wall ends.

With `--play FRAMES --game <GAME_ROOT>`, it patches the level into
`Level_01.NMO` in the folder and plays it headless with libivp physics in
the test base, the autopilot steering the ball along `route_points.json`
from frame 360. With `--sector N` the level starts at sector N, with the
ball the player has there, and the rewards of the sectors before are not
looked for. It writes `run.log` and `steer.csv` there and reports:

- the checkpoints, the finish and any fall, with their frames;
- where the ball stood still a while, and what the point it headed for
  waits for (a ball changer holds the ball 126 frames; a fan point waits
  for a height);
- each extra point or life the ball never came within 4 of before the
  finish. Every reward should lie where a ball can get to it.

It exits with 1 if the level does not finish or the ball falls.

## The Autopilot's Points

Each point of `route_points.json` is `[x, z]`, with more values where the
ball has to wait or rush:

| Point | Meaning |
|-------|---------|
| `[x, z, y]` | Over a fan: wait there until the wind has lifted the ball to height `y` |
| `[x, z, y, frames]` | Hold there for `frames`: a target pushed (switch, pusher, arm), the climbing logs' hole |
| `[x, z, null, "rush"]` | Over a jump's gap: run at it flat out |
| `[x, z, y, "push:N"]` | Head for it N frames, reached or not, or until the ball is `y` high: in the fan tower's bend, the ball backed up and run in again, off the middle by turns, till the wind takes it |
| `[x, z, y, 0, seat, sx, sz, reach]` | Before a swing, or before a sack a beam passes under: wait until object `seat` comes within `reach` of `(sx, sz)` (a sack twice: at one end of its swing, then going out the other side) |

`nmo behavior run --steer` takes them as `X,Z[,...];...@FRAME`.
`ballance.verify.play.steer_text()` formats them that way.

## Watching a Level

Set the game's folder and the nmo CLI built with libivp and the window once,
in the shell's profile:

```sh
export BALLANCE=<GAME_ROOT>
export NMO=<build>/tools/nmo
```

Then one command opens a built level (or a level file) in a window, and the
autopilot plays it as the verifier does; with `--self`, you roll the ball
with the arrow keys:

```sh
apps/ballance/scripts/ballance_play_level.py <out>
apps/ballance/scripts/ballance_play_level.py <out> --self
apps/ballance/scripts/ballance_play_level.py <out> --sector 3
```

The level plays as level 1, in the test base, until the window is closed;
`--sector N` starts it at sector N. The autopilot steps a fixed 1/60 s a frame,
as the verifier's headless runs do, so the ball goes the same way; in real
time a marginal mechanism can go otherwise (the fan tower's ball may stay in
its bend). `--mute` turns the sound off.

Underneath it is `nmo behavior run --window --delta 16.667 --write <dir>
--path <GAME_ROOT> --steer POINTS@360 --steer-object
Ball_Wood,Ball_Stone,Ball_Paper <test base>`: the files are looked for in
`--write` (a folder holding `Level_01.NMO`), then in `--path` (the game's
`3D Entities`), and `--shot FILE@FRAME,...` saves frames.

## The Test Base

`ballance.verify.harness` writes a composition of libnmo's own to play a
level in, in place of the game's `base.cmo`: no intro, no menu, no
tutorial, the level built and started at once (the ball rolls by frame
280, where the game's menu took 1000). The level's gameplay is the game's
own files in `3D Entities` (Gameplay, Levelinit, Balls, Sound, Menu for its
fonts and keys, the PH modules), loaded as `base.cmo` loads them. What
those files ask of `base.cmo`, all by name, the test base has: the `Level`
object their messages go to, the arrays `GameSettings`, `DB_Options`,
`Loading_Parameter` and `RegisterNMO`, and the script `Loading_Manager`,
which Levelinit runs to load the level's modules. `GameSettings` has the
tutorial off and Godmode on, so the game's debug keys work: 3 finishes the
level, 2 gives a life.

A start at sector N goes as the game's own start does: before the level is
built, `AllLevel` is given the ball the player has at sector N and its
reset point, so the ball starts there. The checkpoints before it are then
reached as the game reaches them, all but the last by the test base, which
the ball, standing on it, reaches itself; the skipped ones are stopped.

It is written once to the system's temporary folder (`ballance-harness`,
one file a sector start) with `nmo patch apply`: a project manifest for its
objects, `nmo data fill` for its arrays and an edit plan for its scripts.

`ballance_map_view.py` draws a level, built or original, from above, the
sides, the corners or any angle, with the game's textures if given. It names
every mechanism, element and marker with its sector, and draws the route's
points and a ball's `steer.csv` on top:

```sh
apps/ballance/scripts/ballance_map_view.py <out> --trace <out>/steer.csv --views top,iso \
    --around P_Modul_37_01 35 --textures <GAME_ROOT>/Textures -o view.png
```

## Playing an Original Level

## The Data

| File | Contents |
|------|----------|
| `level_materials.json` | The originals' 48 shared materials, as manifest materials (`ballance_level_materials.py`) |
| `base_ballance_bb_manifest.json` | For rebuilding `base.cmo` ([ballance-base-bb-rewrite.md](ballance-base-bb-rewrite.md)) |

## The Package

The scripts in `apps/ballance/scripts` are thin. The work is in the
`ballance` package beside them (`apps/ballance/ballance`), in layers, each
using only those before it:

| Layer | Contents |
|-------|----------|
| `core` | Headings on the XZ plane, OBJ meshes, the nmo CLI |
| `game` | The groups, element kinds and mechanisms, the measured dimensions (`measures.py`), the materials |
| `kit` | Floors on the 2.5 grid, pieces off the grid, rails, railings |
| `route` | A route laid out (`layout`, `pieces`, `modules`), its fall zones and clashes, written as a manifest |
| `extract` | The element meshes and floors cut from the originals, the materials, the level checks |
| `autoplay` | Paths over a level's ground, as autopilot points |
| `verify` | Floor faults, and a level played through |
| `view` | Drawing levels |

## Not Covered Yet

- No generated level has been loaded in the original game. They are played
  only in libnmo's engine.
- Level_05's pusher frame slides back when the ball stops pushing it; only
  Level_01's pushers are built.
- The ball does not come near a reward hung over a fan, past a fan's
  block, under a fall or in a trough's bottom, so such rewards go
  untaken. Rewards belong on floors the ball rolls along.
- The beams are played only by the autopilot; how hard they are for a
  player at the keys is not known.
- No box pits: a ball that falls into a pit 5 deep cannot get out, and a
  pit 5 long is rolled over.
