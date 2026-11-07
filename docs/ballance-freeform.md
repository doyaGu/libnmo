# Free-form Ballance geometry

Use the [builder workflow](ballance-level-builder.md#from-a-route-to-a-level)
for extraction, build records and report status conventions.
The fragments below describe reusable geometry features. Assemble authored
routes in [local map inputs](ballance-level-builder.md#local-map-inputs);
complete maps and their recorded runs are not distributed with this reference.

## Three-dimensional paths

Paths start at the preceding piece's exit. Coordinates are local
`[forward, right, up]`, relative to that start. Line and Bezier endpoints
are absolute within these local coordinates. Arcs continue from the current
endpoint and tangent. The analytic families below do too.

```json
{
  "rail_path": {
    "segments": [
      {"line": [10, 0, 0]},
      {"bezier": [[20, 0, 0], [30, 15, -5], [40, 15, -5]]},
      {"arc": {"radius": 20, "angle": 90, "rise": 8}}
    ],
    "bank": [[0, 0], [0.4, 8], [0.7, -8], [1, 0]],
    "gauge": 3.8,
    "radius": 0.35,
    "step": 2,
    "tolerance": 0.025
  }
}
```

- `line` gives an endpoint.
- `bezier` gives two control points and an endpoint. Adjacent segments
  must meet with matching horizontal tangents.
- `arc` gives a radius, signed angle in degrees and optional rise.
  Positive angles turn right. A 360-degree arc with a rise is a helix;
  one segment can turn up to four times.
- `bank` rolls the cross section in degrees; positive raises its right side.
- `gauge` is the distance between the two rail tubes' centres; `radius`
  is each tube's radius.

`bank`, `gauge` and `radius` accept constants or `[[fraction, value], ...]`
stations. Fractions increase from 0 to 1 and refer to distance along the
sampled 3D curve. Values interpolate linearly; every station is included
in the mesh. Tubes that overlap are rejected. A valid mesh with an unusual
gauge may still be impassable for the ball.

`step` limits sample spacing. `tolerance` limits the distance of Bezier
control points from each subdivided chord and an arc's chord deviation.
Cross sections stay upright on slopes, then rotate by `bank`. Vertical
tangents and upside-down tracks are not supported.

`beam_path` uses the same curve segments for a single tube. It joins a
standard unguarded rail with the original overlapped height transition and
can continue into `beam_turn`, another beam, or a standard rail. Its entry
and exit headings must be cardinal and its end radii must be 0.35. It
accepts bank and radius stations, but not gauge or decorative ribs.

`beam_turn` accepts a numeric `gap` from 8 to 80 units, defaulting to 8.
It sets the separation of the incoming and returning lanes; the turn radius
is half that separation. Subsequent pieces start at the new exit position.

Single-beam steering points use the opt-in `precision` action: slower
arrow-key control with a shorter lookahead and smaller tracking tolerance.
This changes the automatic driver, not the map physics or manual play.
The clearance checker reserves room for a ball sitting on top of a single
tube; the lower clearance used between double rails is insufficient there.

## Analytic shapes and structure

These bounded families generate geometry directly; they do not evaluate
arbitrary expressions. They work on both rail and floor paths, and mirror
with templates. With `u` ranging from 0 to 1:

- `wave`: `{"length": 160, "amplitude": 12, "cycles": 1, "rise": -42,
  "crest": 6}`. Forward distance is `length*u`; lateral offset is
  `amplitude*sin(2*pi*cycles*u)^3`. Height combines a smoothstep rise with
  `crest*sin(pi*u)^2`. Lateral slope and curvature vanish at the ends,
  allowing a smooth transition into a straight.
- `weave`: `{"length": 170, "width": 100, "lift": 16, "rise": -12}`.
  The horizontal figure eight uses `length/2*sin(t), width/2*sin(2*t)`,
  rotated to meet the incoming tangent. Height is
  `lift*(1-cos(t)) + rise*smoothstep(u)`. The three visits to its centre
  are at heights 0, 26 and -12 for these values. Separation must still
  account for the ball, supporting structure and intersecting approaches.
  `hand: -1` reflects the curve.
- `eased_turn`: `{"length": 100, "angle": 90, "rise": -18}`. Heading is
  `angle*(3*u*u-2*u*u*u)`; integrating it gives the horizontal curve.
  Curvature starts and ends at zero. The implementation uses deterministic
  Simpson integration, then adapts sampling to spacing and deviation.

Periodic families seed 128 intervals before adaptive subdivision, preventing
repeated lobes from disappearing when endpoints and midpoints coincide.

## Death zones and recovery

Omitting `death_zones` preserves the legacy layout. An explicit object enables
surface-following zones:

```json
{
  "death_zones": {
    "tile": 25, "clearance": 18, "quantum": 5, "depth": 35, "margin": 80
  }
}
```

Each occupied cell uses the lowest nearby collision surface. Triangle
footprints are covered in full, including the middle of a long straight
rail. Four units of horizontal padding protect the rotating ball's bounding
box from a neighbouring cell's higher death plane. Decorative lantern
towers and noncolliding braces do not set the height. Lower landings keep
their descent columns clear, with a final global box beneath the map.

For a deliberate recovery volume that the surfaces cannot describe, add
`"keepouts": [{"box": [x0,x1,y0,y1,z0,z1], "reason": "..."}]`. The box
is subtracted from every death volume, including the global one; reserve
only the space actually needed for recovery. The generated `death_zones.json`
records the policy and exact boxes for inspection. Geometry alone cannot
certify a jump: verify intended drops and an intentional failed trajectory
in the gameplay harness.

## Variable-width floors

`floor_path` accepts the same path segments and bank settings:

```json
{
  "floor_path": {
    "segments": [
      {"bezier": [[15, 0, 0], [25, 20, 4], [40, 20, 4]]},
      {"bezier": [[55, 20, 4], [65, 0, 0], [80, 0, 0]]}
    ],
    "width": [[0, 5], [0.25, 12], [0.75, 3], [1, 5]],
    "section": "v",
    "depth": [[0, 5], [0.4, 8], [1, 5]]
  },
  "elements": [{"kind": "P_Extra_Point", "at": [35, 0]}]
}
```

`width` accepts a constant or distance stations. `section` is `"v"`
(the standard road), `"flat"`, or ordered `[width fraction, height]`
pairs from -0.5 at the left edge to 0.5 at the right. Extra points create
ridges, gutters or asymmetric sections. `end_section` morphs toward a
second profile. The two profiles may have different width fractions: the
builder samples both at the union of their knots, preserving each endpoint's
piecewise linear shape.

`depth` accepts a constant or `[distance fraction, thickness]` stations.
Thickness must be within 0.1–100 units. Stations must increase strictly from
0 to 1, including both endpoints; values interpolate linearly along sampled
3D centreline distance. The example thickens from 5 to 8 units, then returns
to 5. Every thickness station becomes a mesh sample, even between ordinary
path samples. Thickness follows the section's banked up axis; the underside,
side walls and exposed end caps use the value at each end of an interval.

For example, `"section": "v", "end_section": "flat"` transitions into
a flat road. Reverse it before reconnecting to a standard road. The
builder rejects a floor whose inner edge folds backward around a tight
bend; widen the turn or narrow the floor. This local check does not prove
that a whole path avoids itself or other geometry.

Flat and custom profiles have brick bands on both edges. Each band is
0.75 units wide, capped at one quarter of the local floor width. It follows
width changes and retains every profile knot. The stone interior and border
are separate surface regions. Transverse UV distance follows the actual
cross section; longitudinal UV distance follows the sampled centreline.
Side-wall UV depth follows the changing thickness. These path bands are
automatic; the `border` option described below belongs to polygon platforms.

Path elements use `[distance, lateral offset]`. Distance follows the 3D
centreline; positive offset is right. Placement follows the curve's height,
bank and section. Rewards, boxes and loose balls are supported; fans still
need existing housing pieces.

## Concave platforms, holes and multiway connections

`floor_polygon` triangulates a simple convex or concave outline without
filling its notches. Self-intersecting outlines, repeated vertices and
ports too wide for their edge are errors.

```json
{
  "floor_polygon": {
    "outline": [[0,-5], [10,-5], [10,-12], [30,-12], [30,5], [0,5]],
    "entry": {"edge": 5},
    "exit": {"edge": 3, "at": 0.7058823529411765},
    "depth": 5
  },
  "elements": [{"kind": "P_Extra_Life", "at": [20, 0]}]
}
```

Outline coordinates are `[forward, right]`. Edge indices are zero-based;
the final edge returns to the first vertex. `at` locates the port along
its edge (default 0.5). Each port accepts its own `width` (default 5,
range 0.5..100); the whole opening must fit its edge. The entry must be centred at
`[0,0]`, facing backward. The exit follows its edge's outward normal,
including diagonal normals.

The builder adds a 5-unit V-to-flat adapter before the outline and a
5-unit flat-to-V adapter after the exit. Each adapter consists of a
2.5-unit V neck and the native 2.5-unit flattening, triangulated from the
low V point on both halves. The outline therefore begins 5
units beyond the preceding piece. With the default zero slope, its top is at
the incoming road's edge height. Port openings are removed from its walls.
Elements use the outline's local coordinates and follow the surface height.

For an indirect route, supply `"path": [[forward, right], ...]` interior
waypoints. Every segment from entry through these points to exit is
checked against the outline and all holes. Holes and named branch ports share
the same local coordinate system, including on an inclined plane:

```json
{
  "id": "hub",
  "floor_polygon": {
    "outline": [[0,-15], [40,-15], [40,15], [0,15]],
    "holes": [[[15,4], [25,4], [25,10], [15,10]]],
    "entry": {"edge": 3},
    "exit": {"edge": 1},
    "ports": {"paper": {"edge": 0, "path": [[20,0]]}}
  },
  "branches": [{
    "id": "paper-spur", "port": "paper", "visit": true,
    "route": [{"id": "approach", "road": 10},
              {"id": "change", "trafo": "paper"},
              {"id": "return", "road": 5}]
  }]
}
```

Up to 32 simple holes may be supplied in either winding. They must lie
strictly inside the outer contour, without touching, overlapping or nesting.
Their brick borders must also fit without intersecting the outer border or
each other. Tops and undersides leave the holes empty; inward-facing walls
close their thickness. Elements and authored paths cannot occupy a hole.
`supports: [[forward, right], ...]` requests platform-owned columns; their
capitals and any permitted braces still undergo attachment checks.

Each named port accepts `edge`, `at`, `width` and an optional `path` from
the entry to that port. Entry, exit and named ports currently require
distinct outer edges; holes cannot host ports. Each outlet owns a five-unit
flat/V adapter and a normal port contract, so branches may start with a
matching floor connector, or a standard rail where the floor port is standard.
Different port widths and non-cardinal edges are supported through free paths;
legacy grid pieces still require cardinal standard ports. Mirror operations transform holes,
paths, support points and transverse slope while preserving port names and
edge indices.

A branch selects its outlet with `port`, replacing the legacy `side` field.
`visit: true` follows the port's internal path and returns along it before
continuing to the main exit. Several visits execute in authored order.
To merge into another polygon, give the destination piece a `name` and
use `to: "destination-name", to_port: "port-name"` on the source branch.
`take: true` selects that branch as the playable route instead of the main
route between the platforms. The destination's internal port path is
traversed in reverse to join its main route. Position, heading and section
must match exactly. A two-ended `connector` can target the named port as
described [below](#connecting-two-ends). Every declared port must have one
branch connection. Missing, duplicate and mismatched connections fail the build.

`connections.json` includes platform identities, world-space hole contours,
ports and source locations. An authored void is a real opening in collision
geometry; centreline validation alone does not prove that the ball has
enough room around it. Verify the intended traversal with actual physics.

### Inclined platforms

Set `floor_polygon.slope` to a forward grade or `[forward grade, right grade]`.
Each grade is rise per horizontal unit in the outline's local coordinates;
`0.1` is equivalent to `[0.1, 0]`. Values must be finite and the gradient's
magnitude must be at most 4. The default is zero.

```json
{
  "floor_polygon": {
    "outline": [[0,-8], [20,-8], [20,8], [0,8]],
    "entry": {"edge": 3},
    "exit": {"edge": 1},
    "depth": 5,
    "slope": 0.1
  }
}
```

This platform rises 3 units between its external entry and exit ports:
20 units across the outline plus two 5-unit adapters. For an incoming
V-bottom height `h` and gradient `[gf, gr]`, the surface at outline point
`[f, r]` has height `h + 0.7 + (5 + f) * gf + r * gr`. The 0.7 offset is
the standard V edge height. Polygon `depth` remains a constant vertical
thickness; floor paths and connectors provide thickness transitions.

The plane also sets hole walls, element positions and support-column heights.
Each outlet derives its own grade from its outward direction. A transverse
grade tilts its V profile: add `fraction * width * cross_grade` to each
standard profile height. It does not set the port's `bank`. Use the section
reported in `connections.json` when connecting to an oblique side outlet.
When approached inward, a port reverses its heading, grade, bank and profile
width fractions. Template mirroring negates the right grade and reflects
the profile with the outline.

Match the incoming connector's endpoint slope to the platform entry grade.
Match its width, depth and profile too; a transverse platform slope requires
the corresponding tilted profile. An outgoing connector can return to zero
grade and the standard floor section. Platform geometry, port matching and
UV projection do not determine whether a ball can climb the chosen slope;
check that approach with native play.

Stone UVs measure distance on the inclined plane. Brick patches compensate
for slope stretch, and side walls use their sloped edge length and vertical
depth. The native V/flat texture cell retains its material phase reset.

## Connections and autopilot

Free pieces may join at arbitrary horizontal headings. Their widths,
banks, depths and sections must match. Direct rail-to-floor connections use
standard ports; an explicit connector can add the transition to another
floor section. Connecting back to legacy pieces requires a cardinal heading,
zero bank, and either a 5-wide, 5-deep V floor or rails with gauge 3.8 and radius
0.35. Put a narrow road between a free floor and a legacy area.

Mesh and steering points share sampled curves. The autopilot still needs
braking instructions for some tight bends. Either path can include
`"stops": [[0.25, 30], [0.75, 30]]`: each pair is a distance fraction and
frames to hold. These affect the autopilot only, not manual play.

Polygon tops have a brick perimeter by default (`border: 0.75`; zero
omits it). The border is a partition of the surface, not a coplanar decal.
Its miters turn into both sides of each port, closing the concave corners
while leaving the centre open. Invalid insets and borders that close a
port are rejected. Wider V floors keep the native brick scale at their
edges and tile their stone interiors rather than stretching one bitmap.
Wide V-to-flat platform mouths use the same fixed-width edge bands.

## Explicit connectors

A `connector` inherits the preceding floor or double rail's section and
morphs it to `to`. Only the end values need to be specified:

```json
{
  "schema_version": 1,
  "sectors": [[
    {"id": "entry", "road": 10},
    {"id": "widen", "connector": {"length": 15, "to": {"width": 8, "bank": 3}}},
    {"id": "level", "connector": {"length": 10, "to": {"bank": 0}}},
    {"id": "platform", "floor_polygon": {
      "outline": [[0,-8], [20,-8], [20,8], [0,8]],
      "entry": {"edge": 3, "width": 8},
      "exit": {"edge": 1, "width": 10}
    }},
    {"id": "narrow", "connector": {"length": 15, "to": {"width": 5}}},
    {"id": "exit", "road": 10}
  ]]
}
```

Choose exactly one of `length`, `segments` or `endpoint`. A positive `length`
builds a straight, level connector; `segments` uses the same local curves
as a path. Floors accept target `width`, `bank`, `section` and `depth`; double rails
accept `gauge`, `radius` and `bank`. Omitted values retain the incoming
section. Values change linearly with sampled arc distance. Custom floor
profiles are resampled at the union of their width fractions, preserving
each endpoint's piecewise linear shape. `step`, `tolerance`, `stops`, and
rail-only `ribs` retain their path meanings.

For example, `{"connector": {"length": 20, "to": {"width": 8, "depth": 3}}}`
widens and thins the incoming floor over 20 units. Its exit has depth 3;
the next floor or platform must match it. Use another connector to restore
the standard 5-unit width, V section, zero bank and 5-unit depth before a
legacy road. A thickness change leaves the top profile unchanged unless
`section` or `bank` also changes.

### Connecting two ends

An `endpoint` supplies the destination pose. The builder solves a cubic
curve from the incoming port, including its grade, to that pose. This route
connects an offset rail to an independently placed platform:

```json
{
  "schema_version": 1,
  "start": {"heading": "+x"},
  "placements": {
    "route/id/destination": {"position": [95, -8, -24], "heading": [1, 0]}
  },
  "sectors": [[
    {"id": "entry", "road": 10},
    {"id": "rails", "rail": 10},
    {"id": "link", "connector": {
      "endpoint": {"placement": "route/id/destination", "slope": 0},
      "to": {"kind": "floor"},
      "min_radius": 8,
      "max_grade": 1
    }},
    {"id": "destination", "floor_polygon": {
      "outline": [[0,-5], [20,-5], [20,5], [0,5]],
      "entry": {"edge": 3}, "exit": {"edge": 1}
    }},
    {"id": "exit", "road": 10}
  ]]
}
```

Replacing the platform with `{"id": "destination", "module": "P_Modul_25"}`
uses the same connection for a native drawbridge. Moving the destination's
explicit placement changes the connector on the next build.

| Endpoint form | Meaning |
| --- | --- |
| `{"offset": [60,20,8], "heading": [1,0], "slope": 0}` | Local `[forward,right,up]` displacement and unit `[forward,right]` end heading |
| `{"placement": "route/id/destination", "slope": 0}` | World pose from the top-level `placements` table; the target must have an explicit placement |
| `{"platform": "hub", "port": "west"}` | An available named polygon port, approached inward; inherits its inward grade and requires the platform to be laid out |

`slope` is rise divided by horizontal distance. It defaults to zero for an
offset or placement endpoint. A named platform endpoint derives its inward
slope from the port; an explicit conflicting value is rejected. The target
section still comes from `to`, so match the destination's width, depth and
profile separately. An unknown incoming slope requires `start_slope`; a known
incoming slope cannot be overridden with a conflicting value. Endpoint exits
require the next piece to have a matching known entry slope. Legacy joints still permit
intentional slope changes.

`min_radius` defaults to 8 and must be positive. `max_grade` defaults to 1
and must be in `(0,4]`. The solver checks horizontal curvature and absolute
grade over the entire cubic through subdivided derivative bounds, independent
of mesh sampling. It tries a finite, deterministic set of tangent lengths.
Optional `handles: [30,40]` supplies the two positive lengths explicitly.
Failure means none of the tested curves could be certified; move the
endpoint, change the handles or deliberately change the limits. This is
not an obstacle-routing search.

An endpoint connector from a double rail to `to.kind: "floor"` reserves a
five-unit floor dock before the requested endpoint. The rail ends at a standard
section, five horizontal units before the destination and `5 * slope` units
below it. The dock starts with a standard V floor and transitions to `to`'s
width, depth, bank and profile while maintaining the destination grade.
Put elements on a separate floor after this connector. Other kind changes
use the existing standard-port adapters. Beam overlaps, ballistic transfers
and module interiors retain their measured adapters. Unusual gauges and
cross sections need gameplay tests.
Template mirroring reflects local endpoint offsets and headings, target
bank and custom profiles; placement and named-port references remain references.

The builder writes `connections.json` with endpoint poses, known slopes,
sections, source IDs, opening and UV policies, and authored transitions.
Positions use the route's V-bottom datum, with rail centres offset above it.
Direct joints check positions, horizontal headings and sections at tolerance
`1e-6`. Endpoint-generated transitions also record their generated paths and
radius/grade constraints. A failure names the ports and source locations.

Consecutive free floors carry their cumulative UV distance through the
joint. The native grid and the platform's ProfileFlat image retain explicit
material phase resets. This does not promise seamless texture phase across
every native module. Platform openings and connector mouths share their
width; neither emits an internal end cap. A rail entering a free floor
closes the exposed floor end wall. Custom and flat profiles use
`Floor_Top_Border` on their edge bands and `Floor_Top_Boderless` inside;
standard V and native V/flat adapters retain their profile materials.

## Spatial constraints and scenery attachments

Native builds (`--elements`) use the same spatial query for placement and
exported-scene validation. It covers rail tubes, the ball's swept clearance,
the balloon's departure envelope, authored floor and landing corridors,
and separation between scenery assemblies.
Scenery fitting uses bounded rail and ball sweeps with conservative scenery
bounds; it is not an exact mesh collision test. Scenery placement applies
a 0.5-unit route-clearance margin. The
balloon envelope has a `finish` state and is checked during placement even
though the balloon has not yet departed.

The builder writes `spatial_constraints.json` with volume owners, rules,
states, source locations, and attachment bindings. `scenery_placement.json`
records changes and their causes. After exporting, the builder checks the
actual meshes and transforms before publishing the generation. An invalid
attachment or unresolved conflict fails the build and retains the previous
generation. Stand-in builds do not validate native scenery attachments;
using placement rules without native assets produces a warning.

Use `scenery.rules` to configure an accessory by its ID from the bindings:

There are three placement modes:

- `locked` keeps the original frame. Native module supports use this default.
- `nearby` moves a lamp and its supporting column together, preserving height.
  Lamps use this default and cannot be reparented.
- `attached` permits a platform pillar to move only while its capital remains
  under its declared parent. Platform pillars use this default. A different
  `parent` must name an existing route entity; supports cannot use `nearby`
  to discard their attachment.

Platform contact uses the declared parent's region plus triangle probes at
the capital's four corners and centre. Native floors often omit underside
faces; in that case the check uses the measured floor thickness below the
sampled surface. This is a conservative attachment check, not proof of full
surface contact or structural support. An unrelated platform cannot satisfy
the attachment merely because a relocated column is beneath it.

Two explicitly authored finish poles must lie symmetrically on opposite
sides of the balloon. They are locked as a pair; automatic fitting cannot
move or discard one. An empty `finish_poles` list still removes them.
Lamp IDs use marker and accessory indices, so inserting a checkpoint can
change checkpoint lamp IDs. Platform accessory IDs derive from the route
entity ID and its local index; preserve both when editing rules.

Native motion recipes extend this query through finite per-body envelopes.
Top-level `passages` reserves floor corridors and lower-layer landing discs
in the owning route element's entry frame. The verifier checks ball clearance
against physical triangles, samples floor support at the declared layer,
and checks native motion against the corridors. Fresh play also checks
observed landings. See [passage and landing constraints](ballance-spatial-constraints.md)
for the schema, tolerances and report fields.

Free bodies, unlimited sliders, camera envelopes, undeclared floor routes
and gaps between support samples remain coverage limits. A passing spatial
report is not a full gameplay or visibility guarantee.

## Native resources and mechanisms

Named resources share the route adapters, extraction provenance and placement
checks described in [Ballance native resources and motion](ballance-native-resources.md).
That reference covers all 67 descriptions, the thirteen motion recipes,
per-body envelopes, intentional interactions and original-level probes.

## Gameplay conditions

`gameplay.contracts` declares required passages, native events and sustained
mechanism actions. Passage and event conditions use this form:

```json
{
  "gameplay": {
    "contracts": [
      {"id": "paper-ready", "at": "route/id/paper-exit", "ball": "Paper"},
      {"id": "lift-exit", "at": "route/id/fan", "ball": "Paper", "after": ["paper-ready"]},
      {"id": "checkpoint", "event": "Checkpoint reached", "sector": 1, "after": ["lift-exit"]}
    ]
  }
}
```

`at` resolves a route entity's exit, with a ball-centre height offset. Its
3D observation radius defaults to 4 units and must be in `(0, 10]`; choose
a passage beyond the transformer, not its activation centre. Ball names
are `Wood`, `Stone` and `Paper`. Native events currently include
`Checkpoint reached`, `Level_Finish`, `Ball Off`, `Life_Up` and `Extrapoint`,
received by `All_Gameplay`. Events carry their actual sender and recipient
IDs. Repeated score messages are not interpreted as distinct collected items.

Dependencies name earlier contracts and cannot point into a later sector.
The builder rejects unresolved entities, invalid conditions and cycles;
`gameplay_contracts.json` stores resolved positions and source locations.
The CLI appends `object_id` and CSV-escaped `object_name` to `steer.csv`.
Checks use this observed identity, not the ball type predicted by the route.
A wrong-ball visit, missing passage or event before its prerequisites fails
even if the run reaches the finish. Later visits cannot erase an earlier
violation. Checkpoint messages delimit sectors, preventing another layer or
an earlier sector visit from satisfying a condition.

`--play` adds a `gameplay` check. No contracts or no fresh run is explicitly
skipped. A partial-sector run marks earlier contracts as skipped setup
assumptions. Endpoint observations alone do not establish mechanism completion. Use an
`action` condition for a sustained hinge rotation or a ball rise, then test
correct play, incorrect conditions and death recovery with the scenario runner.
See [actions and replayable scenarios](ballance-gameplay-scenarios.md) for
the schema, commands and evidence requirements. These authored cases do not
exhaust arbitrary shortcuts or player strategies.

## Parameterized templates

Top-level `definitions` hold reusable local sequences:

```json
{
  "definitions": {
    "bend": {
      "parameters": {"radius": 20, "angle": 90},
      "pieces": [{"rail_path": {"segments": [
        {"arc": {"radius": "${radius}", "angle": "${angle}"}}
      ]}}]
    }
  },
  "sectors": [[
    {"road": 10},
    {"use": "bend", "with": {"radius": 30}, "repeat": 2, "mirror": true},
    {"road": 10}
  ]]
}
```

An exact `${parameter}` string substitutes its JSON value, including a
number, list or object. No expressions or code are evaluated. Templates
can call other templates and appear inside branches. Recursion and
unknown parameters are errors; expansion is limited to 4,096 pieces.

`mirror` swaps left/right and reflects coordinates, banks, cross sections,
elements, branches and the right grade of a polygon's slope. `repeat` continues
each copy from the previous copy's exit. Parameterize named branch targets to
keep repeated names unique.

## Instance overrides and placement

Use route `schema_version: 1` and give template calls and their pieces stable
`id` values before storing per-instance edits. Top-level `overrides` maps an expanded entity ID from
`source_map.json` to a `set`/`remove` patch. For a template `strip` containing
`{"id":"road","road":20}`, called as `{"id":"copy","use":"strip"}`:

```json
{
  "overrides": {
    "route/id/copy/repeat/0/id/road": {"set": {"road": 25}}
  }
}
```

Overrides apply after parameter substitution and mirroring, in the expanded
leaf's incoming local frame. Only that instance changes. Patches cannot change
IDs, geometry kinds or branch topology, and a field cannot appear in both `set`
and `remove`. An override whose instance disappeared is an error; restore the
instance or remove its override explicitly. Changing a shared definition still
changes every call that uses it.

Top-level `placements` maps a route entity ID to an entry pose:

```json
{
  "placements": {
    "route/id/landing": {"position": [20, 5, -10], "heading": [1, 0]}
  }
}
```

Position is world `[x, y, z]` at the route's V-bottom datum. Heading is a finite
unit `[x, z]` direction. Grid pieces retain 90-degree headings; `rail_path`, `floor_path`,
`floor_polygon` and `connector` permit arbitrary horizontal headings. Placement checks the real
previous exit against the moved entry. Disconnected joints are recorded in
`connections.json` and fail the connection check; the builder does not invent a
connection to hide a gap. Native mechanism placement and physics use the separate
[instance settings](ballance-native-resources.md#per-instance-placement-and-physics).

## Build and validate

Geometry and route tests do not need a game installation:

```sh
python3 -m unittest discover -s apps/ballance/tests -p test_freeform.py -v
python3 -m unittest discover -s apps/ballance/tests -p test_floor_transitions.py -v
```

The floor-transition tests check thickness stations, different profile knots,
closed walls, brick width, UV seams, rotated and mirrored ports, and inclined
platforms with holes. Their native case skips unless physics and assets are
enabled. With the environment below, it plays two generated routes through
custom floors and inclined platforms, including a rail-to-floor approach.
The other physics suite covers a helix, mirrored banked floor bends and a
concave platform in libnmo's native gameplay harness:

```sh
export BALLANCE=/path/to/Ballance
export BALLANCE_ELEMENTS=/path/to/extracted-elements
export NMO_TEST_CLI=/path/to/nmo
export NMO_TEST_PHYSICS=1
python3 -m unittest discover -s apps/ballance/tests -p test_freeform_physics.py -v
python3 -m unittest discover -s apps/ballance/tests -p test_floor_transitions.py -v
```

Use absolute paths. These small fixtures exercise shared code without a local
map. Retain map-specific builds, full-route runs and reports locally; see the
[regression coverage guide](../CONTRIBUTING.md#ballance-toolchain-regression-coverage).
