# Ballance Level Format

What a Ballance level file has to contain, and how to write one with libnmo.

The rules below were read from the 13 original levels and from the data arrays
of `3D Entities/Levelinit.nmo`, and checked by running generated levels in
libnmo's behavior engine. `<GAME_ROOT>` is the Ballance installation folder.

## The Level File

A level is one file, `<GAME_ROOT>/3D Entities/Level/Level_NN.NMO`. It is data
only. All 13 original levels hold these five classes and nothing else:

| Class | Level_01 | Level_12 |
|-------|---------:|---------:|
| CK3dObject | 128 | 218 |
| CKMesh | 128 | 206 |
| CKMaterial | 41 | 37 |
| CKTexture | 30 | 28 |
| CKGroup | 29 | 36 |

There are no scripts and no scene. `base.cmo` loads the file and reads it
through group membership and object names; every behavior (physics, the
moving elements, checkpoints, the finish) comes from scripts outside the
level.

## Where the Rules Live

`3D Entities/Levelinit.nmo` keeps them in data arrays. To read them:

```sh
nmo object list --class CKDataArray "<GAME_ROOT>/3D Entities/Levelinit.nmo"
nmo data dump <id> "<GAME_ROOT>/3D Entities/Levelinit.nmo"
```

| Array | Contents |
|-------|----------|
| `Physicalize_Floors` | Groups made fixed physics objects: `Phys_FloorRails`, `Phys_Floors` (collision group Floor) and `Phys_FloorStopper` (collision group Ball). Friction 0.7, elasticity 0.3. |
| `Physicalize_Convex` | Element groups physicalized as movable convex objects: `P_Box`, `P_Ball_Paper`, `P_Dome`. |
| `PH_Groups` | The 23 element groups, with how each activates and resets. |
| `DepthTestGroups` | `P_Ball_Paper`, `P_Ball_Wood`, `P_Ball_Stone`, `P_Box`. |
| `AllLevel` | Per level number: level file, start ball, sky, light, music, level bonus. |

`AllLevel` is indexed by level number, so a level file cannot choose its sky,
light or music: they belong to the slot it is loaded into.

`3D Entities/Sound.nmo` has `BallSound`, which names the rolling and hit
sounds per ball and surface (stone, wood, metal).

## Groups

### Markers

| Group | Members | Meaning |
|-------|---------|---------|
| `PS_Levelstart` | `PS_FourFlames_01` | The start. One per level. |
| `PE_Levelende` | `PE_Balloon_01` | The finish balloon. One per level. |
| `PC_Checkpoints` | `PC_TwoFlames_NN` | Checkpoints. |
| `PR_Resetpoints` | `PR_Resetpoint_NN` | Where the ball spawns and respawns, one per sector. |

A level has as many sectors as reset points, and one checkpoint fewer
(Level_01: 4 sectors, 3 checkpoints; Level_12: 8 and 7). Passing
`PC_TwoFlames_N` ends sector N and starts sector N + 1, whose reset point is
`PR_Resetpoint_{N+1}`.

### Elements

Elements are placeholders. The object in the level gives a position; when the
level loads, the game copies the real object, with its scripts and physics,
from a template in `<GAME_ROOT>/3D Entities/PH/` and puts it there. The
templates of `PE_Balloon` and the `P_*` elements bring their own meshes. Those
of `PS_FourFlames` and `PC_TwoFlames` hold no mesh: the game draws the start's
flame pillars and the checkpoints with the level object's own mesh, so these
two need the game's meshes, not a box.

`apps/ballance/scripts/ballance_level_elements.py <GAME_ROOT> -o <dir>` exports one
mesh of each kind from the original levels, and the `SkyLayer` plane, as
`<Kind>.obj`. The meshes are the game's, so they come from a local
installation; nothing of them is kept with libnmo.

The start's octagonal platform and the pillars' bases are not the start
object: they are a floor like any other (`Floor_Top_Checkpoint` on top), and so
is the round floor with the two flame bowls under a checkpoint. In the
original levels they are usually part of a larger floor mesh. Their marker
sits at the middle of the floor's top, and their rims are 1.26 above it.
Where a road meets them, the rim has a gap, 2.6 wide at the start and 1.74 at
a checkpoint. Level_01's start and first checkpoint have no such gap for a
floor road (its checkpoint meets rails), but Level_03's do: its start opens
on its +X side 7.39 from the marker, and its first checkpoint on both X sides
4.92 from it. `ballance_level_elements.py` cuts these two out of Level_03's
floors and writes them around the marker, as `Floor_Start.obj` and
`Floor_Checkpoint.obj`. In Level_03 the floors are unturned, the road runs
along +X, the start is turned -90 degrees (its X axis along +Z) and the
checkpoint is unturned.

Each element is in two groups: its kind, and its sector.

- Kind: `P_Extra_Life`, `P_Extra_Point`, `P_Trafo_Paper`, `P_Trafo_Stone`,
  `P_Trafo_Wood`, `P_Ball_Paper`, `P_Ball_Stone`, `P_Ball_Wood`, `P_Box`,
  `P_Dome`, and `P_Modul_01`, `_03`, `_08`, `_17`, `_18`, `_19`, `_25`, `_26`,
  `_29`, `_30`, `_34`, `_37`, `_41`. Elements are named after their kind:
  `P_Box_03`, `P_Modul_18_07`.
- Sector: `Sector_01`, `Sector_02`, ... An element is only active while its
  sector is. Only elements go in sector groups.

Levelinit looks up every kind group by name. A level without one of them gets
one error at load and nothing else; the original levels lack most kinds
(Level_01 has 11 of the 23).

### Floors and Rails

Floors, rails and decoration are ordinary meshes. Their role comes only from
their groups:

| Group | Role | Original members |
|-------|------|------------------|
| `Phys_Floors` | Floors the ball rolls on | `A01_Floor_01`, `A03_Wood_02`, `A05_Halfpipe_01`, `A02_Modul42` |
| `Phys_FloorRails` | Rails | `A01_Rail_02`, `A01_Geländer_01` |
| `Phys_FloorStopper` | Stoppers, in collision group Ball | `A02_Floor_Stopper` |
| `Sound_RollID_01`, `Sound_HitID_01` | Stone sounds | floors |
| `Sound_RollID_02`, `Sound_HitID_02` | Wood sounds | wood, Modul, Halfpipe |
| `Sound_RollID_03`, `Sound_HitID_03` | Metal sounds | rails |
| `Shadow` | Surfaces that receive the ball's shadow | floors |

The `A<sector>_` prefix of the original names is a modelling convention; the
game goes by the groups.

### The Fall Zone

`DepthTestCubes` holds a few large boxes (`Quader01`, ...) 30 to 80 units
below the track. The ball touching one of them is what loses a life ("Ball
Off" and a respawn at the current sector's reset point). Every level needs
them:

- with the group empty, a ball that leaves the track falls forever;
- without the group, the depth test reports an error every frame.

The boxes are hidden in the original levels, and they have to be: a visible
one is drawn (a giant grey box under the track, hiding the sky). Hidden means
the CKObject visibility flag off, which `"visible": false` sets in a manifest.

### The Sky

Every original level has a `SkyLayer` object: a plane of clouds, about 1900
units wide, 100 to 180 units below the track, with the `SkyLayer` material.
The sky box itself comes from `base.cmo` (per level number, see `AllLevel`).

## Positions

Y is up. Measured on Level_01:

- `PS_FourFlames_01` and each `PC_TwoFlames_N` sit at the middle of their
  floor's top, 1.26 below its rim and the edges of the road that meets it.
- `PR_Resetpoint_01` is 3.65 above `PS_FourFlames_01`.
- `PR_Resetpoint_{N+1}` has the x and z of `PC_TwoFlames_N` and is 3.33 above
  it.
- The ball (radius about 2) comes to rest about 2 above the floor top.
- At a reset point the camera looks along the -X axis of the reset point's
  matrix; Up rolls the ball that way.

## Roads

The floors are built on a 2.5-unit grid, in pieces 5 long, with one texture
repeat every 5 units. Measured on Level_01 and Level_03:

- The narrow road is a channel 5 wide. Its top is two `Floor_Top_Profile`
  slopes that dip 0.7 from its edges to its middle, mapped with one repeat
  across the whole width (u 0 and 1 at the edges, 0.5 in the middle), so the
  texture's brick bands run along the edges.
- The wide road is 10 wide: the same two slopes, each 2.5 wide, with a flat
  `Floor_Top_Boderless` strip 5 wide between them.
- The walls are `Floor_Side`, 5 deep below the edges; u runs down the wall,
  one repeat over its depth. A road has no bottom and, where it ends under
  something else, no end wall.
- Where a narrow road meets a floor's rim, or ends under the balloon's planks,
  a `Floor_Top_ProfileFlat` piece 5 long flattens it to the height of its
  edges, fanning from the channel's middle to the rim's gap. The balloon's
  planks reach 22.08 along its +X to the road's end, which is 0.01 above the
  balloon.
- Wide areas are `Floor_Top_Boderless` with `Floor_Top_Flat` borders 2.5 wide
  that rise 0.7 to their outer edge. `Floor_Top_Border` is the piece that
  turns a wide road's end into a flat edge.

All of these follow one rule: a floor's top is flat inside and rises 0.7 to
every edge of its outline over one 2.5 cell. Two cells wide, that is the
narrow road; four, the wide road; a rectangle, an area with its border; an L,
a turn, whose outer corner is a `Floor_Top_ProfileFlat` cell. A road enters
an area only through a gap two cells wide in front of its middle: the rest of
the area's edge stays raised, so a wide road narrows into the area through an
X-shaped `Floor_Top_ProfileFlat` piece with raised shoulders, and the area's
border cells at the gap are `Floor_Top_ProfileFlat` too. Where a wide road
goes on as a narrow one, its end cells beside the narrow one are raised
corners. `apps/ballance/scripts/ballance_floors.py` builds floors by this rule from
rectangles of areas and roads, which may slope along their length.

## Rails

The rails the ball rolls on (in `Phys_FloorRails`, `Sound_RollID_03`,
`Sound_HitID_03`, no `Shadow`) are two tubes whose middles are 3.8 apart.
Measured on all 13 levels:

- A tube is an octagon 0.35 in radius whose two lowest faces are left out.
  Its normals are smooth; the two at the open bottom lean as their one face
  does. The rings stand upright, square to the track, so a straight rail is
  a ring at each end and a slope changes at a kink.
- A bend is always the same: 90 degrees about a point 8.1 to the side of the
  track's middle (the inner tube 6.2 from it, the outer 10), in nine steps of
  10 degrees.
- Where a rail meets a floor, it stops at the floor's edge with no cap, the
  tubes' middles 0.14 above the bottom of a V the floor opens to it: a narrow
  road's V runs on to its end, an area opens through the same two-cell gap a
  narrow road enters by, and a floor flat at its edges' height meets it
  through the 5-long funnel. The wall under the edge follows the V, its
  bottom level 5 below the edges. A rail that ends over nothing is capped.
- Their UVs follow no rule: Levelinit's "set Env. Mapping" maps every object
  in `Phys_FloorRails` to the environment at load.
- Some `*_Rail_*` objects are not rails but railings: thinner tubes along a
  floor's edge.

`apps/ballance/scripts/ballance_rails.py` builds rails by these rules, and
`Floors.dock()` opens a floor to one.

## Elements on the Floor

Measured on all 13 levels, from each element's position to the floor under
it:

- `P_Extra_Point` 2.5 above it, `P_Extra_Life` 2.56: their meshes rest on it.
- `P_Box` (5 wide) and the balls `P_Ball_*` (radius 2) rest on it.
- `P_Dome` (13 wide, 3.55 high) stands on a flat floor.
- The ball changer nearest the balloon turns the ball back to wood in every
  original level: no level lets a paper or stone ball onto the balloon's
  bridge, a chain of 0.5-mass planks between the platform (mass 4, hung from
  a spring of constant 20) and the road. The balls weigh 0.2 (paper), 1.9
  (wood) and 10 (stone) (`Physicalize_GameBall` in `Balls.nmo`).
- A ball changer (`P_Trafo_*`) stands 0.1 above a 5 by 5 block of its own,
  whose top and sides are `Floor_Trafo_Paper`, `_Stone` or `_Wood` and whose
  top is as high as the road's edges. A narrow road runs into it from either
  side through the same 5-long `Floor_Top_ProfileFlat` funnel that ends a
  road under the balloon.
- `P_Modul_18`, the fan (132 of them in 12 levels), stands in a housing that
  is part of the floor: a 5 by 5 pit, 3.4 deep, with a `Modul18_Gitter`
  grille and a round `Modul18_Top` rim flush with the floor around it. The
  fan stands on the grille, 0.35 below the floor; its wind box reaches 21.4
  up. Fans are set into areas' flat insides, over rails, halfpipes and wood
  floors; in Level_03 a 15 by 15 area holds one in its middle and the next
  15 by 15 area adjoins it 13.5 higher, so a paper ball rides the wind up
  and rolls across.
- `P_Modul_25`, the drawbridge (35 in 12 levels), is an upright plank 9.6
  tall hinged at the edge of a gap 10 long. The ball rolls along the
  module's -X: a narrow road flattens through a funnel onto a flat
  `Floor_Top_Flat` block 5 long at its edges' height, the ball pushes the
  plank over the gap, and it lands on a `Floor_Stopper_Wood` lip (an object
  of its own, only in `Phys_FloorStopper`) at the start of a second block 5
  long, from which a funnel leads back to the road. The module stands at the
  first block's far edge, as high as it.
- `P_Modul_37`, the switch (22 in 8 levels), is a plank 9 wide and 13 tall,
  hinged 3.73 into a target block 5 long at the end of a spur, with a
  bullseye on its arm. A stone ball pushing the target tips it over (in
  about 300 frames; a wood ball does not): it falls across a gap 10 long and
  lands, on a `Floor_Stopper_Wood` lip, in the gap 9.5 wide between the two
  blocks of a cross piece, which funnel down to narrow roads 9.75 from its
  middle. The track runs across the cross piece, over the plank. In Level_05
  (`P_Modul_37_02`) the spur comes down a rail ramp and loose stone balls
  wait near its top.
- `P_Modul_26` (22, in levels 8 to 13) is a sack on a rope, hung from 15
  above it, that swings across the track. The levels hang 1 to 4 in a row,
  10 to 16 apart, every other one turned the other way, 2.68 above a flat
  road 5 wide at a narrow road's edge height (`Floor_Top_Flat` squares of
  5, one texture repeat each) or over rails.
- `P_Modul_41` (43, in levels 7 to 11) is a slab 5 square and 1.7 thick on
  an axle across the track. The levels lay 2 to 8 in a row along the
  module's X, 6 apart, at a road's edge height, with a gap of 1 on either
  side of each: the floors before and after the row (a flat strip, or a
  funnel up from a narrow road) stop 3.5 from the end slabs' middles, closed
  by a wall 5 deep. In the engine only the paper ball crosses a row; a wood
  or stone ball tips the slabs and falls between them.
- `P_Modul_01` stands on `Modul01_Floor`; `P_Modul_03`,
  `_08`, `_19`, `_29`, `_30` and `_41` hang over gaps with no floor under
  them.

`ballance_floors.py` builds the ball changer's block with `pad()` and leaves
a fan housing's cells out of an area with `Floors.hole()`;
`ballance_level_elements.py` cuts the housing out of Level_03 as
`Floor_Modul_18.obj`, and the drawbridges' blocks and stoppers, in the
modules' own frames, as `Floor_Modul_25.obj` and `Floor_Stopper_25.obj`
(Level_03) and `Floor_Modul_37.obj` and `Floor_Stopper_37.obj` (Level_05).
`channel()` lays a narrow road of any length off the grid, where a piece
lands between grid lines.
`strip()` builds the flat road under the sacks.

## Scenery

Around the track the originals stand pillars, lantern towers and striped
poles, each a copy of the same meshes, moved (measured on all 13 levels):

- A pillar is a blue cap 4.7 wide and 4.3 tall, a shaft 3.9 wide down to 20
  below its top, a shaft 2.7 wide down to 98.6 below, and a third that fades
  out down to 328 below (`Floor_Colums_Bottom`, `Columns_beige_fade`).
  Pillars and their shafts are in no group.
- Under a floor, a pillar stands 2.5 in from a corner of an area, its top 5
  below the floor's edge, where the floor's walls end. No original stands one
  under a start or a checkpoint.
- A lantern tower (`*_Tower`) stands on a pillar's top: a 5 by 5 base, four
  posts, a lantern and a roof, 17.7 tall. Towers are in `Phys_Floors`, in no
  sound group. They stand in pairs beside the track: 24 to 28 ahead of the
  start, 13 to either side, about 2.4 below it; about 20 ahead of a
  checkpoint, 11 to 15 to either side, about 5 below it; and around the
  balloon.
- Level_01, Level_02, Level_04 and Level_13 stand three pairs of striped
  poles (`Stick_Top`, `Stick_Bottom`) by the balloon.

`ballance_level_elements.py` cuts one pillar, one tower and one pole out of
Level_01 as `Pillar.obj`, `Tower.obj` and `Stick.obj`.

## Writing a Level with libnmo

A project manifest describes the whole level: meshes from OBJ files, groups,
and textures that stay in the game's `Textures` folder.

```json
{
  "version": 1,
  "document": {"name": "MyLevel"},
  "groups": [
    {"name": "PS_Levelstart", "objects": ["PS_FourFlames_01"]},
    {"name": "PC_Checkpoints"}
  ],
  "scenes": [{"name": "MyLevel", "objects": [
    {"name": "A01_Floor_01", "class": "CK3dObject",
     "mesh": {"obj": "floor.obj"},
     "material": {"texture": "Floor_Top_Flat.bmp", "texture_save": "external"},
     "groups": ["Phys_Floors", "Sound_RollID_01", "Sound_HitID_01", "Shadow"]},
    {"name": "PS_FourFlames_01", "class": "CK3dObject",
     "mesh": {"obj": "marker.obj"},
     "transform": {"position": [0, -0.2, 0]}}
  ]}]
}
```

- Root `groups` are created in their order, and may be empty. `objects` lists
  members by object name or id.
- An object's `groups` adds it to each group, creating any group that is not
  listed at the root.
- `"visible": false` saves a 3D object hidden, as the fall zone boxes are.
- `"texture_save": "external"` stores only the texture's file name, the way the
  original levels do; the game finds the file in its texture folders. Textures
  of the same file are one CKTexture, named after the file
  (`Floor_Top_Flat`). The default, `"embedded"`, stores the image.

### Materials

The original levels share one set of 48 materials, named like
`Floor_Top_Flat`, `Floor_Side`, `Rail`, each the same in every level.
`apps/ballance/data/level_materials.json` holds them as manifest materials; it is
written by

```sh
apps/ballance/scripts/ballance_level_materials.py <GAME_ROOT> -o apps/ballance/data/level_materials.json
```

Root `materials` are shared: each becomes one CKMaterial with its own name.
An object uses one by name (`"material": "Rail"`), and an OBJ mesh uses one
for each `usemtl` group of the same name, so a mesh exported with the game's
material names needs nothing else. An object's own `materials` entries win
over shared ones of the same name.

Besides colors, `texture` and `blend`, a material can set:

| Key | Meaning |
|-----|---------|
| `texture_format` | Pixel format the engine keeps the texture in: `argb1555` (the originals' opaque textures), `argb8888` (their textures with alpha), `rgb888`, `rgb565`, `argb4444` |
| `alpha` | `{"func": "greater", "ref": 1}`: alpha test, on when `func` is not `always` |
| `two_sided` | Draw both faces |
| `z_write` | Write depth (on by default) |

Generated from the presets, 46 of the 48 materials match the originals bit
for bit. `Columns_beige_fade` and `Stick_Bottom` keep an alpha function and
reference with alpha testing off in the originals; the presets leave both at
their defaults, which draws the same.

```sh
nmo patch apply --project manifest.json -o Level_01.NMO
```

The generated file also holds a CKScene and an object named after the
document. They made no difference in the engine runs.

`apps/ballance/scripts/ballance_build_level.py <route.json> <out> [--elements <dir>]`
builds a level from a route: its pieces sector by sector (roads narrow or
wide and coming down, areas left from the side, turns, ball changers, rails
straight or bent, fans lifting the ball to the next area, drawbridges, a
switch, rows of slabs, swinging sacks), with elements placed on the floors. It lays them
out on the floors' 2.5 grid with
`ballance_floors.py`, turns the start, the checkpoints and the balloon with
the track, stands the scenery as above, hides the fall zone under all of it,
and writes `route_points.json`, the points along the middle of the track.
With the meshes `ballance_level_elements.py` exported, it uses the game's
start, checkpoint and element meshes, opens the road into Level_03's start
and checkpoint floors and adds the `SkyLayer`; without, boxes stand in for
the markers, elements and those floors, and there is no scenery and no sky.
The script's docstring describes the route format.

## Checking a Level

```sh
apps/ballance/scripts/ballance_check_level.py [--json] Level_01.NMO
```

reads a level with the nmo CLI and reports what this document's rules say:

- errors, when the level does not play: no start or end, reset points and
  checkpoints that do not number `_01`.. or do not add up, no `Phys_Floors`, no
  `DepthTestCubes` group;
- warnings, when it plays wrong: an empty or visible fall zone, an element in
  no sector or not in its kind's group, a reset point away from its checkpoint,
  a start or checkpoint whose mesh is a placeholder box (the game draws them
  with the level's own meshes), no `SkyLayer` object (every original level has
  one);
- notes, for what the game gets over: missing element or sound groups (one
  load-time error each), visible floors without sound groups.

It exits with 1 when there are errors. The 13 original levels give no errors
and no warnings.

Groups can be changed in place with the CLI:

```sh
nmo group add -n Phys_Floors A01_Floor_07 A01_Floor_08 Level_01.NMO -o Level_01.NMO
nmo group list --member A01_Floor_07 Level_01.NMO
```

## Testing a Level in the Engine

`nmo behavior run` runs `base.cmo`. `--path` puts a folder in front of the
game's own, so a `Level_01.NMO` there replaces level 1:

```sh
cd <GAME_ROOT>
nmo behavior run --physics ivp --path <out> -n 3300 \
    --keys 'Enter@800-805,Enter@900-905,Up@2760-3300' \
    --registry 'Software\Ballance\Settings\Language=1' base.cmo
```

`--physics ivp` needs a build with libivp. The two Enter presses start
level 1; the ball spawns around frame 2700. The log names the messages the
level's events send to `All_Gameplay`: `Extrapoint`, `Checkpoint reached`,
`Ball Off` (a fall), `Level_Finish`.

A track that turns needs steering: `--steer` rolls the ball through the
points of `route_points.json` with the arrow keys, as a player would:

```sh
nmo behavior run --physics ivp --path <out> -n 4500 \
    --keys 'Enter@800-805,Enter@900-905' \
    --steer "$(python3 -c "import json; print(';'.join(','.join('%g' % v for v in point) for point in json.load(open('<out>/route_points.json'))))")@2760" \
    --steer-object Ball_Wood,Ball_Stone,Ball_Paper \
    --registry 'Software\Ballance\Settings\Language=1' base.cmo
```

A point over a fan has a height (`X,Z,Y`): the ball waits there until the
wind has lifted it that high. The point at a switch's target has a frame
count too (`X,Z,Y,F`): the ball keeps pushing there for F frames. The sample plays to the finish this way (frame
5449): an extra point, the sacks, the first checkpoint, the stone ball
changer, the extra life, the drawbridge, the second checkpoint, the paper
ball changer, the slabs, the rails, the fan, the extra point on the area
above it, the wood ball changer and the balloon.

Not covered yet: levels generated this way have not been loaded in the
original game, the moving modules other than the fan, the drawbridge, the
switch, the slabs and the sacks (`P_Modul_*`) are not placed, and rails neither meet wide
roads nor end over nothing.
