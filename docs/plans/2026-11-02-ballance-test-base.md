# A Test Base for Ballance Levels

Status: implemented in
[`ballance.verify.harness`](../../apps/ballance/ballance/verify/harness.py).
This record replaces the original proposal with the loading and sector-start
behavior used by the level verifier, window player and native motion probes.

The generated composition plays a level without the original `base.cmo`.
It skips the intro and menu flow while loading the game's own gameplay and
physics modules. Runs use libnmo and libivp; loading the generated level in
the original game remains a separate compatibility check.

## Loading Native Gameplay

The harness creates a `Level` object, a `Test Start` script and a
`Loading_Manager` script. The manager loads the array named by
`Loading_Parameter`, from its start row through its end row (`-1` means the
last), and starts each file's initialization script when requested.
`Levelinit_build` uses the same manager to load the level's PH modules.

The main files are `Language.nmo`, `Sound.nmo`, `Menu.nmo`, `Gameplay.nmo`,
`AnimTrafo.nmo`, `Camera.nmo`, `Levelinit.nmo` and `Balls.nmo`. Menu scripts
stay stopped; gameplay uses the menu file's `Keyboard` array and fonts.
The harness does not create a replacement keyboard database.

`Test Start` loads these files, activates `Levelinit_build`, then starts
`Gameplay_Ingame` and the level sound. The generated arrays are
`GameSettings`, `DB_Options`, `Version`, `MainLoading`, `Loading_Parameter`
and `RegisterNMO`. Arrays such as `AllLevel` and the native physicalization
tables come from the loaded game files.

`GameSettings` has Godmode on, Debug off and the tutorial off. Godmode makes
the native debug keys available: 3 finishes a level and 2 grants a life.
The verifier supplies steering points and observes the actual finish;
these keys are not its completion procedure.

## Starting at a Sector

The builder records each sector's starting ball in `route_sectors.json`.
The player and verifier pass both sector and ball to `harness.ensure()`.
When starting beyond sector 1, or starting with a ball other than wood,
the harness changes row 0 of the native `AllLevel` array before building
the level: `StartBall` selects the ball and `StartResetpoint` is `sector - 1`.

Earlier checkpoints are advanced through the native messages after gameplay
starts. The harness waits 4000 milliseconds before the first injected
checkpoint and 1500 milliseconds between further advances. The ball itself
reaches the final checkpoint beneath its starting position. Earlier sectors'
rewards and authored gameplay conditions are skipped setup assumptions in a
partial run.

`setup_ms(sector)` returns zero for sectors 1 and 2, otherwise
`4000 + 1500 * (sector - 2)`. Normal autoplay begins at frame
`360 + ceil(setup_ms(sector) / FRAME_MS)`, with `FRAME_MS = 16.667`.
The delay lets sector activation settle before steering. Sector selection
is compiled into the harness; there is no runtime `--array` selector.

The public commands use this setup automatically:

## Generating and Caching the Composition

`harness.write()` creates a project manifest, fills its arrays with
`nmo data fill`, and applies a version-2 edit plan for the scripts. The
loading manager is called by other scripts rather than started independently
with the scene.

Normal harnesses are cached in the system temporary directory under
`ballance-harness`. Sector 1 with wood uses `test_base.cmo`; other starts use
`test_base_<sector>_<ball>.cmo`. The sector and ball are both part of the
cache key, including stone or paper starts at sector 1. A cache file is
regenerated when absent or older than the harness module. This cache uses
file modification times; artifact provenance checks record content hashes
separately.

## Isolated Native Contact Probes

`harness.write()` also accepts a probe position and native ball physics
parameters. The dedicated probe command writes a separate `probe_base.cmo`
into each result directory, so probe placement does not alter the ordinary
harness cache.

After `6000 + setup_ms(sector)` milliseconds, a probe removes the player's
physicalization, moves the player and creates a two-unit sphere using the
native ball table's parameters. Native mechanism bodies keep their original
setup. The level file itself is a byte-identical copy of the installed
original. The verifier requires the spawn and the intended movable bodies
to be observed; Plates also requires its split state.

