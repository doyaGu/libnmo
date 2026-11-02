# Ballance Level Builder

How to build, check and play a Ballance level with the tools in
`apps/ballance`. What a level file holds, and the measurements the builder
follows, are in [ballance-level-format.md](ballance-level-format.md).

The tools are Python scripts. They read and write levels through the `nmo`
CLI (`--nmo`, or `$NMO`, or `nmo` on `PATH`) and take the game's meshes and
textures from a local installation, `<GAME_ROOT>`. Nothing of the game is
kept with libnmo.

## What You Need

- A Ballance installation, `<GAME_ROOT>`, with `base.cmo`.
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
```

`ballance_build_level.py` lays the route out and writes into `<out>`:

| File | Contents |
|------|----------|
| `manifest.json` | The level as a libnmo project manifest, with the OBJ meshes it names |
| `route_points.json` | The points along the middle of the track, start to balloon, for the autopilot |
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
- **Rails:** straight, coming down, humped or dipped (Level_12's zigzag,
  with sacks hung under the crests), bent, or turned back on themselves
  through a hairpin. A rail ends on a floor, in a fall into an area, a log,
  a trough or a diamond, in Level_01's cradle, in a fan's block, or in
  Level_04's fan tower, whose three channels wind down round its shaft to a
  way out on the left.
- **Logs and troughs:** Level_12's half logs crossing one under another,
  Level_06's logs climbing with fans, and Level_04's troughs, hopped between
  or with holes in their bottoms.
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
it does not reach at its side's middle and height.

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
  rail it is dropped from.

## Verifying a Level

`ballance_verify_level.py SOURCE` checks the floors of a built folder or a
level file (`ballance.verify.geometry`): walls left open at their ends,
holes in floor tops, and tops of two objects lying on one another. Some are
meant: the cradle's pocket is a hole, and Level_01 has 21 open wall ends.

With `--play FRAMES --game <GAME_ROOT>`, it patches the level into
`Level_01.NMO` in the folder and plays it headless with libivp physics, the
autopilot steering the ball along `route_points.json`. It writes `run.log`
and `steer.csv` there and reports:

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
| `[x, z, y, 0, seat, sx, sz, reach]` | Before a swing: wait until object `seat` comes within `reach` of `(sx, sz)` |

`nmo behavior run --steer` takes them as `X,Z[,...];...@FRAME`.
`ballance.verify.play.steer_text()` formats them that way.

## Watching a Level

In a window, with the same keys and autopilot the verifier uses, from the
game's folder:

```sh
cd <GAME_ROOT>
nmo behavior run --window --mute --physics ivp --path <out> -n 12000 \
    --keys 'Enter@700-705,Enter@725-730,Q@860-1000' \
    --steer "$(PYTHONPATH=<libnmo>/apps/ballance python3 -c 'import json, sys; from ballance.verify.play import steer_text; print(steer_text(json.load(open(sys.argv[1]))))' <out>/route_points.json)@1000" \
    --steer-object Ball_Wood,Ball_Stone,Ball_Paper \
    --shot <dir>/f4000.png@4000,<dir>/f8000.png@8000 \
    --registry 'Software\Ballance\Settings\Language=1' base.cmo
```

`--path` puts the folder before the game's own, so its `Level_01.NMO`
replaces level 1. The two Enters start level 1 as soon as the menu takes
them, and Q skips the tutorial, which otherwise holds the ball 25 seconds.
`--shot` saves frames, and runs with it step a fixed 60 Hz so their frame
numbers repeat. Without `--steer`, the ball is the keyboard's. Leave
`--window` out to run headless.

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
- No box pits: a ball that falls into a pit 5 deep cannot get out, and a
  pit 5 long is rolled over.
