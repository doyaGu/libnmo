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

Elements are placeholders. The object in the level only gives a position;
when the level loads, the game copies the real object, with its scripts and
physics, from a template in `<GAME_ROOT>/3D Entities/PH/` and puts it there.
The marker objects above work the same way (`PS_FourFlames`, `PE_Balloon`,
`PC_TwoFlames` have templates too), so any mesh will do for a placeholder.

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

## Positions

Y is up. Measured on Level_01:

- `PS_FourFlames_01` sits about 1.25 below the floor top.
- `PR_Resetpoint_01` is 3.65 above `PS_FourFlames_01`.
- `PR_Resetpoint_{N+1}` has the x and z of `PC_TwoFlames_N` and is 3.33 above
  it.
- The ball (radius about 2) comes to rest about 2 above the floor top.
- At a reset point the camera looks along the -X axis of the reset point's
  matrix; Up rolls the ball that way.

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
     "transform": {"position": [0, -1.25, 0]}}
  ]}]
}
```

- Root `groups` are created in their order, and may be empty. `objects` lists
  members by object name or id.
- An object's `groups` adds it to each group, creating any group that is not
  listed at the root.
- `"texture_save": "external"` stores only the texture's file name, the way the
  original levels do; the game finds the file in its texture folders. Textures
  of the same file are one CKTexture, named after the file
  (`Floor_Top_Flat`). The default, `"embedded"`, stores the image.

### Materials

The original levels share one set of 48 materials, named like
`Floor_Top_Flat`, `Floor_Side`, `Rail`, each the same in every level.
`tools/ballance/level_materials.json` holds them as manifest materials; it is
written by

```sh
tools/scripts/ballance_level_materials.py <GAME_ROOT> -o tools/ballance/level_materials.json
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
`Ball Off` (a fall), `Level_Finish`. The sample level sends all of them; steer
off the track with `Left` after the checkpoint to see a fall and a respawn at
`PR_Resetpoint_02`.

Not covered yet: levels generated this way have not been loaded in the
original game, and the moving elements (boxes, modules, trafos) have not been
driven in a generated level.
