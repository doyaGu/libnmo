# Ballance native resources and motion

[`resource_catalog.json`](../apps/ballance/data/resource_catalog.json) owns 67 resource descriptions covering all 99
native mesh kinds exported by the element tool: every `P_Modul_*` mechanism,
ball changer, reward, loose ball, box, dome, native static assembly, scenery
component, marker, pad and sky plane used by the existing route adapters.
It records native source files and objects, extraction frames
and cuts (including coordinate snapping), visible/collision/support roles, groups, fallback boxes, local
ports and their height datums, required ball types, attachments and runtime
contracts. FanTower refers to the existing `P_Modul_18` motion recipe;
Balloon owns the finite departure corridor. Compatibility constants in
`game.measures` read these descriptions rather than keeping separate values.

List the available placement entries or inspect a complete description:

```sh
python3 apps/ballance/scripts/ballance_resource_catalog.py
python3 apps/ballance/scripts/ballance_resource_catalog.py Pusher
python3 apps/ballance/scripts/ballance_resource_catalog.py --json
```

Refresh selected resources from a local game installation without re-extracting
every unrelated module:

```sh
python3 apps/ballance/scripts/ballance_level_elements.py <GAME_ROOT> \
  -o <ELEMENTS_DIR> --resources FanTower Balloon --nmo <path-to-nmo>
```

Full element extraction also writes `resource_index.json`. It binds each
description to source level/template hashes and the emitted OBJ/MTL hashes,
and inventories the runtime template's mesh-bearing entities, local mesh
bounds and world matrices. A level's balloon mesh is a placement marker;
the PH template supplies the runtime balloon assembly. Its visual mesh
inventory is not a reconstruction of every physical collision hull.

Native builds require this extraction record. Changed descriptions or
changed/missing extracted files require a refresh. The build emits
`resources.json`, binding descriptions and provenance to placed instances,
world-space ports, scene objects and attachment outcomes. An explicitly
omitted fan-tower support remains omitted; refreshing its mesh does not
place it back into the level.
Native scene objects without a resource binding, or bindings without native
source provenance, fail resource verification.

Verification adds a `resources` check and compares the recorded native
sources against `--game`. Changed sources block gameplay and native preview.
Reports and runtime evidence include these dependencies. Stand-ins and
checks without a game installation explicitly retain review status for
native-resource coverage. Port coordinates retain their declared native
datums: fan entry is a tube centre, fan exit and balloon entry are flat
surfaces; the existing route adapters handle their V-floor transitions.

The named mechanism route entries use the same measured traversal adapters
as their native `P_Modul_*` equivalents. For example:

```json
{"resource":"TiltingSlab", "count":4, "id":"paper-crossing"}
```

`Pusher`, `Lift`, `Swing`, `Flaps`, `Plates`, `Seesaw`, `StoneBox`,
`RotatingArm`, `Drawbridge`, `SwitchBridge`, `TiltingSlab` and `Sack` accept
the existing module parameters; `SwitchBridge` still requires its stone-ball
branch. An area's element can use `{"resource":"Fan", "at":[5,5]}`.
Templates, repetition and mirroring work with these entries. The descriptor
owns count limits and ball restrictions. The drawbridge distinguishes its
standalone floor/stopper assembly from the bridge embedded in a rotating-arm
loop, where the parent supplies the surfaces. Mechanism instance IDs link
resource records to the existing motion checks.
The paired pushers share their first marker's corridor frame. `BoxMaze`
places three `StoneBox` mechanisms on one shared maze assembly, without
duplicating their standalone plazas.

Other route entries retain the native adapters' traversal requirements:

| Resources | Entry and parameters |
| --- | --- |
| `PaperChanger`, `StoneChanger`, `WoodChanger` | A changer with its pad and funnels on a narrow road |
| `ArchedPlank`, `WaveWalkway`, `LongWaveWalkway`, `Twist`, `Jump` | Native wooden route assemblies on a narrow road |
| `Spiral` | Native spiral from a wide road |
| `Cradle`, `FanTower` | Enter from a rail; retain their measured landing/lift rules |
| `FanBlock` | `rise` specifies fan lift, default 10 |
| `RailSplit` | `drop` specifies the fall, default 10; follow with a landing |
| `Octagon` | `exit` and one `branches` entry connect the native node |
| `Diamond` | Landing after a rail fall |
| `BoxMaze` | Three stone-box mechanisms sharing the native maze |
| `Log_1` … `Log_8`, `Trough_1` … `Trough_8` | Native lengths of 50 … 400; existing turn/drop or hop rules apply |
| `PerforatedTrough` | The native 350-long trough with holes |
| `LogClimb` | A lead-in log (`length`, default 50) followed by the native fan climb |

On grid or free floors, `elements` accepts `ExtraPoint`, `ExtraLife`, `Box`,
`Dome`, `PaperBall`, `StoneBall` and `WoodBall`, for example
`{"resource":"ExtraPoint","at":[10,0],"up":2}`. Box piles retain `pile`.
Ball changers use their dedicated route entry, rather than an arbitrary
element position, so their pad and traversal semantics remain intact.
The fan element still requires an area's housing.

`RailingClamp` is placed by road/area railings, `RailDock` by wide-road/rail
transitions, and start/checkpoint/reset markers, pads, fan housings and sky
by their existing level builders. These entries describe their real owning
adapter; they are not loose objects that can be placed without their parent.
The `elements` and `module_floors` binding paths also cover components created
inside other route adapters, not just explicitly named resource entries.

Scenery can be authored relative to a route piece's **exit frame**:

```json
{
  "scenery": {
    "instances": [
      {"id":"hub-lamp", "resource":"LanternTower",
       "parent":"route/id/hub", "at":[-7.5,25,-4.3]}
    ],
    "rules": {"scenery/id/hub-lamp":{"mode":"nearby", "max_shift":10}}
  }
}
```

Use route `schema_version: 1` and an authored piece `id` for a stable parent.
`at` is `[along, right, up]` from the exit's V-bottom datum; the resource
origin is the column's top centre. `LanternTower` always places its lamp and
column together. `Column` uses attached mode and must meet the owning
platform's underside; a typical column top is 4.3 below the V datum.
Both pass through the normal clearance, support and optional brace rules.
Lanterns retain the existing conservative exclusion over grid-floor and
checkpoint footprints, including their initial authored position.
They remain upright in the world; this interface does not scale native meshes.
`FinishPole` remains a balloon accessory authored through `finish_poles`,
not a free scenery instance. Stand-in builds validate references but skip
native scenery placement with a warning.

Native mechanism traversal ports use their measured adapters. The resource
inventory and motion recipes serve different checks: source coverage does
not guarantee a finite movement bound. All thirteen native mechanism kinds
have recipes; free bodies and unlimited sliders retain explicit unbounded
motion status. The balloon's 150-unit corridor covers the standard visible
departure. Unlimited flight, UFO endings, arbitrary replacement templates
and complete discovery of game dependencies remain outside these checks.

## Native mechanism states and motion

The measured catalog covers all thirteen native mechanism kinds used by the
builder. Rotating arms (`P_Modul_17`), fans (`18`), drawbridges (`25`), sacks
(`26`), switches (`37`) and slabs (`41`) retain their hinge, joint-reach or
force-field recipes. Seven assembly recipes add per-body constraints:

| Resource | Native kind | Motion contract |
| --- | --- | --- |
| `Pusher` | `P_Modul_01` | Free pusher body; fixed corridor and filler |
| `Lift` | `P_Modul_03` | Unlimited slider for the floor; seven free walls and a free gate; the spring applies force without a hard travel limit |
| `Swing` | `P_Modul_08` | Unlimited hinge with native alternating drive; full-turn reservation |
| `Flaps` | `P_Modul_19` | Unlimited hinge for the flaps; fixed axle |
| `Plates` | `P_Modul_29` | Nine plates and ten hinges; anchored chain reach before and after the break |
| `Seesaw` | `P_Modul_30` | Unlimited hinge; full-turn reservation |
| `StoneBox` | `P_Modul_34` | Unlimited slider for the stone and a free box |

Disabled native limit flags take precedence over saved limit values. A
slider can be constrained to an axis without having a finite travel range.
Free bodies and unlimited sliders appear under `unbounded_motion`; static
validation retains `review` because it cannot reserve all future positions.
Intentional interaction declarations cannot waive this coverage limit.

For the plates, the native stone-ball proximity logic destroys hinge 188,
between plates 06 and 07. Both halves remain anchored. The `split` state
uses the surviving joint paths to bound each plate's reach from its anchor;
it does not turn the nine plates into free bodies. Reset recreates the
hinges and restores the template pose.

The seven assembly recipes separate ideal geometry from a numerical
allowance of 0.5 units per joint. Reach contracts accumulate this allowance
along their joint path; hinge and slider contracts use one allowance.
Reports retain the resulting `solver_margin`. This allowance is a solver
tolerance, not an inferred native travel limit.

Each placed instance has its own ID, sector, source location, state names
and activation/reset transitions. `inactive` reserves no pose;
`ready` describes the template pose; `moving` reserves the reachable motion.
Placement checks all states, including those belonging to other sectors.
The state descriptions are native behavior contracts, not evidence that a
switch actually fired or a reset completed during play.

Native hinge templates with disabled angular limits reserve a full turn
unless actual collision geometry certifies blocking poses on both sides
of the restored pose. Ten-degree intervals
reduce empty space; analytical extrema
keep the bounds conservative between interval endpoints. Sack reach covers
both free ball joints: the rope length plus the sack's maximum distance
from its lower joint. Spherical probes reject impossible corners of this
reach box. Fans reserve both the rotating body and their native force-field
mesh; the field is identified as an effect, not a solid obstacle.

The catalog contains measured mesh bounds, frames, pivots and template
SHA-256 hashes, without including native meshes. Refresh it with the local
installation and the current CLI:

```sh
python3 apps/ballance/scripts/ballance_motion_catalog.py \
  --game <GAME_ROOT> --nmo <path-to-nmo> \
  --out apps/ballance/data/motion_catalog.json
```

The catalog participates in the generator fingerprint. Verification with
`--game` checks the native template hashes; a mismatch fails the mechanism
check and skips gameplay until the recipes are refreshed and the map rebuilt.
Unknown mechanism kinds remain explicitly `unsupported`, making the report
`review`. They are not treated as motionless objects that passed a check.

The `mechanisms` report compares motion against foreign rails, ball
clearance, conservative floor bounds and other mechanisms. A module's own
floor and route are intentional contacts. The rotating arm and drawbridge
generated together are a coupled actuator pair. Other adjacent route pieces
remain checked. Potential contacts include both owners, state, body,
location and source configuration. Scenery fitting also reserves these
motion volumes, preventing a decoration from being moved into a later pose.

Intentional cross-piece interactions can be described explicitly:

IDs, phases and contact kinds must resolve exactly. A reason is required;
unknown fields, duplicate pairs and broad selectors are rejected. Matching
contacts remain in `expected_interactions`, with their design reason.
This declaration records intent; it does not prove that the interaction is
playable. Other objects and states retain their checks. Plate interactions
can also name the `split` phase.

Gameplay now writes `physics.csv` through `nmo behavior run --physics-log`:
one row per movable physical body after each simulation step, including
frame, runtime ID, CSV-escaped name and all sixteen world-matrix values.
The log is bound to the build, CLI and native motion templates. Failed or
missing logs cannot reuse previous successful observations.

The `mechanism_motion` report matches bodies by their template names and
initial placements, then follows runtime IDs. It records physicalization
ranges, first displacement beyond 0.1 units, maximum corner displacement,
sampled `observed_box` bounds, unexpected conservative pose contacts, and
motion-envelope escapes. Each body is checked against its own contract;
another body's larger volume cannot conceal an escape.
`moving` in these observations means displaced from the initial pose, not
a measured gameplay event or nonzero velocity. Mesh-bound corners permit
0.1 units of trace tolerance in addition to the recipe's solver allowance.
Slider checks also test orientation and transverse displacement while
leaving axial travel unbounded. Free-body observations establish finite
sampled poses only. Analysis ends at the finish frame.
Nonphysical fan rotors and force fields are not present in this log and
remain explicitly unobserved.

These checks use native body mesh bounds, not exact collision manifolds.
Certified stops assume continuous rigid hinge motion and solid native
supports; they do not prove the solver cannot tunnel. Other full-turn
reservations can include poses prevented by contact with a module's own
floor; pose AABBs can also include empty space. Conversely, a
single successful trace cannot clear an unobserved state or prove that all
between-frame contacts, reset paths and player strategies are safe.

## Probing the seven assemblies in original levels

The dedicated probe command uses a byte-identical copy of each original
level and a separate generated harness. It leaves the installed levels and
native mechanism graphs unchanged:

```sh
python3 apps/ballance/scripts/ballance_verify_native_motion.py \
  --game "$BALLANCE" --nmo "$NMO" --out out/native-motion-seven
```

`--resources Pusher Lift Swing Flaps Plates Seesaw StoneBox` selects a
subset; the default runs all seven. `--frames` defaults to 1600 per probe
and must be positive. The sources are Level_01 for Pusher and StoneBox,
Level_02 for Plates and Seesaw, Level_07 for Lift, Level_08 for Swing, and
Level_09 for Flaps. The current marker adapter accepts translation and yaw;
tilted or scaled markers fail rather than being flattened silently.

After sector activation, the harness waits `6000 + setup_ms(sector)`
milliseconds, unphysicalizes the player, moves it to the probe position,
and recreates its two-unit sphere using the native ball table's mass,
friction, elasticity, collision group and damping. The mechanism bodies
keep their original physicalization. The run must observe the requested
ball near that position, find every required movable body and record actual
displacement. Plates additionally require an observed split: the broken
hinge endpoints must separate by more than twice the per-joint allowance.
This is observed separation, not an independent observation of the native
trigger message.

Each resource directory contains `Level_01.NMO`, `probe_base.cmo`,
`runtime.log`, `steer.csv`, `physics.csv` and `validation.json`. The aggregate
`validation.json` records `complete` and each probe's result. Reports include
original/copy hashes, template agreement, harness and CLI hashes, ball
parameters, spawn observations, displaced bodies and trace findings. A
failed or incomplete probe must not be counted as coverage.

These are local contact probes. A passing trace means the sampled motion
satisfies the available contracts; free bodies still have no finite global
clearance guarantee. Full original-level completion and all possible player
approaches require separate runs.

