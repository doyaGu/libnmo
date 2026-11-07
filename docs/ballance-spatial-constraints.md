# Ballance passage and landing constraints

Use top-level `passages` to reserve the routes a ball must be able to take
across a floor or onto a lower layer. The declarations follow their owning
route elements when those elements move. They distinguish required play
space from unused platform edges and decorative surfaces.

## Coordinates and dimensions

`owner` must identify a route element with a recorded entry pose. Points are
**ball centres**, in `[forward,right,up]` coordinates relative to that entry.
The vertical origin is the entry's route height, usually the V-bottom datum.
Choose the centre height from the actual floor profile; an offset of 2 is
appropriate only where the ball rests on a flat surface at that datum.
For a heading `(dx,dz)`, the right vector is `(dz,-dx)` in Ballance's
left-handed, Y-up world.

| Field | Contract |
| --- | --- |
| `id` | Unique, nonempty passage ID |
| `owner` | Stable route entry ID |
| `kind` | `floor` or `landing` |
| `points` | 2–512 finite ball-centre points; consecutive points must be distinct and at most 1000 units apart |
| `half_width` | Permitted lateral movement of the ball centre, default 0, range 0–20 |
| `landing_radius` | Horizontal recovery disc around the final centre, default 0, range 0–20; valid only for `landing` |

The checked ball radius is 2 units. A `half_width` of 1 therefore reserves
clearance for the sphere along lanes extending one unit to either side of
the centreline; it does not describe a two-unit-wide floor.

A landing path must never rise between points, and its final height must
be below its first height. It declares a descending clearance corridor,
not a launch velocity or a ballistic solution. Use native play to establish
that the intended approach reaches its landing disc.

The builder resolves these declarations into `spatial_constraints.json`.
Its `passages` list contains world points and source pointers; `occupancy`
adds `floor passage` and `landing clearance` volumes alongside the existing
rail, balloon and mechanism reservations. Separate passage IDs remain
separate contacts even when they belong to the same route element.

## Static verification

For a built folder with a route snapshot, `ballance_verify_level.py` adds a
`clearance` check. It compares continuous radius-2 sphere sweeps with the
actual world triangles in `Phys_Floors`, `Phys_FloorStopper` and
`Phys_FloorRails`. A penetration greater than 0.05 units is reported. Mesh
bounds are used to find candidates; a platform hole is not filled by its
bounding box during the triangle test.

Floor corridors use parallel lanes spaced at most 0.5 units apart. Along
each lane, support is probed at intervals no greater than 0.5 units. Each
probe looks 2 units below the declared centre for an upward-facing
`Phys_Floors` triangle at the requested height, within 0.25 units. A floor
on another layer cannot satisfy that probe. Landing support uses the final
disc, with concentric rings and circumferential samples at most 0.5 units
apart. The descending sweeps and the ball at each landing-disc sample are
also checked for clearance.

The report retains these limits:

- Clearance along each lane is continuous; support and the width between
  lanes are sampled. Narrow gaps can fall between probes.
- Only declared floor corridors and landing discs receive support checks.
  A zero count is not a claim that every floor route is traversable.
- A clear descending corridor does not establish launch speed, recovery
  control or clearance for every possible fall trajectory.
- Camera envelopes and full player strategy coverage remain unmodeled.

Automatic rail checks use nominal sphere centres derived from rail
sections. At support joints and faceted bends, the checker can try an
upward fit of at most one ball radius, rechecking all static obstacles at
each candidate height. It records accepted offsets in `rail_support_fits`.
This tests geometric feasibility, not the dynamic path the ball will take.
Remaining nominal-rail contacts retain `review` severity.

Authored passage penetration or missing support makes `clearance` fail.
Potential native motion contacts and unbounded motion require review.
The report lists each passage's support sample count, static findings,
rail segment count, motion contacts and expected interactions. Read the
overall status as well: a failed passage can coexist with a successful
full-route play test. `--strict` changes geometry-finding severity; it does
not turn every conservative motion or rail finding into a failure.

## Motion through a passage

`passages` is an optional selector on an interaction rule. Its IDs must
belong to `target`, and selected rules may use only `floor passage` and
`landing clearance` contact kinds. Unknown IDs, empty or duplicate selectors
and mismatched owners are rejected. Omitting the selector retains the
existing owner/contact/phase matching behavior.

Matching contacts remain in `expected_interactions` with the design reason.
The declaration does not establish playability. Native play's
`mechanism_motion` check also retains the specific passage ID, including
contacts within the owning assembly. Use
[action contracts and recovery scenarios](ballance-gameplay-scenarios.md)
to verify what the intended contact accomplishes.

Native envelopes and observed body bounds are conservative. An intersection
needs inspection; it is not an exact solver collision manifold. See the
[native motion reference](ballance-native-resources.md#native-mechanism-states-and-motion)
for supported recipes and unbounded bodies.

## Observed landings

A fresh `--play` run adds `landings` to `validation.json`. For each declared
landing in the requested starting sector or a later sector, the checker
requires a ball observation within 2 units of the departure point, followed
within 600 frames by three consecutive observations in the landing disc and
height layer. Arrival must be more than one unit below the observed departure.
Horizontal arrival tolerance is `max(0.5, landing_radius)`; vertical
tolerance is 0.5 units.

Frames must remain consecutive, with the same runtime ball ID and name.
A per-frame jump of 20 units or more breaks continuity. The result records
departure, landing and confirmation frames and the observed ball identity.
Missing arrivals fail the check; without a fresh run it is skipped. These
samples confirm arrival proximity, not a solver contact event or every
possible launch speed.

## Validate a local route

After building your own route into `out/level`, run a fresh native check with
`BALLANCE` and `NMO` set to absolute paths:

```sh
python3 apps/ballance/scripts/ballance_verify_level.py out/level \
  --play 24000 --game "$BALLANCE" --nmo "$NMO"
python3 apps/ballance/scripts/ballance_check_evidence.py out/level/validation.json
```

Choose a frame budget that covers the intended run. Without `--play`, static
checks still run, but no new landing or mechanism-motion evidence is collected.
Keep authored passage declarations with the local route and generated reports
in its ignored output directory. Repository tests exercise the checker with
small fixtures; they do not provide completion evidence for your map.

