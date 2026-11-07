# Ballance actions and replayable scenarios

A ball passing a fan or a bridge does not establish that the mechanism did
its job. Action contracts measure a sustained native hinge rotation or ball
movement. Scenario suites repeat a chosen approach with the correct ball,
an incorrect condition, or a death and retry in the same native world.

These checks run in libnmo's IVP test base. Build the level with its native
assets first; see the [level builder](ballance-level-builder.md) for setup.

## Action contracts

Each `gameplay.contracts` entry chooses exactly one of `at`, `event` or
`action`. Actions resolve a stable mechanism instance ID and derive their
sector from that instance. Do not add `ball`, `radius` or `sector` to an
action contract. `after` uses the same earlier-contract dependencies as
passage and event checks.

A `rotation` action requires a hinge body described by the motion catalog.
The angle is measured about its hinge axis relative to the placed native
rest pose. `min_degrees` is in `(0,180]`; `direction` is `positive`, `negative`
or `either` (default). This measures displacement from rest, not cumulative
turns. `hold` defaults to 3 and accepts 1–600 consecutive frames.

A `ball_region` action uses a mechanism-local box in
`[xmin,xmax,ymin,ymax,zmin,zmax]` order. Its bounds must be finite and increase
on each axis. Optional `rise` requires the same observed ball to move from a
lower box, gain at least `min_height`, and still be ascending in the target
region. `within` accepts 1–3600 frames; `min_height` must be positive.

A fall into the upper region does not satisfy this rise condition. Gaps,
teleports or a changed ball identity break the ascent evidence. Ball regions
use the mechanism's native XYZ axes, rotated by its placed yaw; they do not
use the route path's `[forward,right,up]` convention.

The builder resolves these actions into `gameplay_contracts.json`. Full
verification reads actual ball IDs from `steer.csv` and hinge transforms
from `physics.csv`. Bodies bind once by native name and initial placement,
then follow their runtime IDs. Missing, ambiguous or invalid poses cannot
satisfy an action. Nonphysical fan rotor animation is not lift evidence.

## Running a suite

Suites describe the mechanisms and approaches in an authored map, so keep them
with that map's local inputs. The runner is part of the reusable toolchain;
a fresh clone does not include a map-specific suite. After building your local
route into `out/level`, set `BALLANCE` and `NMO` to absolute paths and run:

```sh
python3 apps/ballance/scripts/ballance_verify_scenarios.py \
  out/level .local/ballance/scenarios.json \
  --out out/scenarios --game "$BALLANCE" --nmo "$NMO"
```

The paths above are placeholders for your own build and suite. Add
`--scenario <case-id>` to run one of its cases. The runner checks build and
resource provenance, packs the level, and copies its generated native runtime
variants into each case directory. It clears old generated runtime overlays
before a rerun. Keep `--out` separate from the build directory, and run full
verification and scenario replay sequentially: both pack `Level_01.NMO` in
the source build.

The suite format is `{"version": 1, "scenarios": [...]}`.

Case IDs must be unique and match `[A-Za-z][A-Za-z0-9_.-]*`. `frames` is
600–24000 total simulation frames, including setup. Each case names a native
mechanism, a `Wood`, `Stone` or `Paper` ball, a nonempty `conditions` list,
and a required `guard` action. All actions must resolve in the starting
mechanism's sector. The guard establishes that the intended test area was
actually visited.

| Field | Meaning |
| --- | --- |
| `start: "probe"` | Default. Requires mechanism-local XYZ `spawn`; places and rephysicalizes the selected ball once, using its original ball-table physics |
| `start: "checkpoint"` | Uses the sector's native checkpoint position and starting ball; forbids `spawn` |
| `route_to` | A same-sector route entity whose exit resolves to exactly one steering point; retains the full checkpoint route prefix through that point |
| `points` | Alternative to `route_to`: mechanism-local XYZ steering points, converted to horizontal XZ waypoints |
| `exit` | Additional mechanism-local XYZ waypoint, used horizontally to steer off the route for a recovery case |

Choose exactly one of `route_to` and `points`. Local waypoint Y values do
not create height waits; use `route_to` to retain the route's existing waits
and ball-changer approach. A `recover` case requires `route_to`, a fall
`exit`, and the checkpoint's starting ball. Use `start: "checkpoint"` when the
case must exercise the approach from the native reset point.

## Expected outcomes and recovery

| `expect` | Required observations |
| --- | --- |
| `complete` | Correct initial spawn, successful guard and every sustained action before the first death or the frame limit |
| `blocked` | Correct spawn and guard, no condition succeeds, and complete evidence for every condition throughout that first-attempt window |
| `recover` | Initial completion, native `Ball Off`, checkpoint respawn, hinge rest-pose restoration where applicable, and repeated completion before the next death |

Each condition needs at least 30 observed samples. A positive case can have
missing frames outside its successful sustained interval; a negative case
cannot infer absence from a missing trace. A `blocked` result applies only
to that case's observed window, not to every possible solution.

Recovery runs keep the engine and world alive through death and retry. They
do not inject mechanism poses or restart the process. Respawn must occur
within 900 frames of death and preserve the ball type observed at death.
For hinge conditions, 30 consecutive samples must restore the rest rotation
within 3 degrees and position within 0.15 units. The repeated action is then
checked after respawn.

## Reports and replay evidence

`scenarios.json` summarizes the suite. Each `<out>/<case-id>/` contains:

| File | Contents |
| --- | --- |
| `input.json` | Resolved case, exact native command, level and harness hashes |
| `steer.csv` | Observed ball poses and IDs, waypoint progress and actual key inputs |
| `physics.csv` | Physical body IDs and transforms after each simulation step |
| `runtime.log` | Native messages, including death and checkpoint events |
| `validation.json` | Spawn, guard, action, negative-evidence and recovery results |

The runner uses a fixed 16.667 ms step and attaches evidence sidecars to
these files and the suite report. Exit 0 means all selected cases passed;
1 means a case failed; 2 means an invocation, input or runtime error.
Check a retained report before relying on it:

```sh
python3 apps/ballance/scripts/ballance_check_evidence.py out/scenarios/scenarios.json
```

Keep the suite, raw traces, reports and evidence sidecars local. The same boundary
applies to authored tolerance and shortcut specifications used by
`ballance_verify_tolerance.py` and `ballance_verify_shortcuts.py`. Their runners
remain tracked; concrete approaches and recorded outcomes belong to the map.

These are bounded authored-input tests. They do not enumerate shortcuts,
prove that a level has no soft locks, or establish manual-play difficulty.
