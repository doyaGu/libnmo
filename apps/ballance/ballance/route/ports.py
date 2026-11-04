"""Route ports and direct joints shared by free shapes and native adapters.

Positions use the route's V-bottom datum; rail centres sit ABOVE_V above it.
Headings point along travel on both entry and exit ports. Cross sections stay
upright on slopes, as the existing Ballance meshes do (not a Frenet frame).
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
import math

from ..core.geometry import HEADINGS
from ..kit.floors import NARROW, WALL
from ..kit.rails import GAUGE, TUBE, Ring
from ..kit.shapes import V_SECTION
from .identity import Origin

EPSILON = 1e-6


def cardinal(direction):
    return next((d for d in HEADINGS.values() if math.dist(direction, d) < EPSILON), None)


def profile_height(profile, x):
    for (u, a), (v, b) in zip(profile, profile[1:]):
        if x <= v + EPSILON:
            return a + (b-a) * (x-u) / (v-u)
    return profile[-1][1]


def same_profile(a, b):
    return all(abs(profile_height(a, x)-profile_height(b, x)) <= EPSILON
               for x in {p[0] for p in (*a, *b)})


@dataclass(frozen=True)
class Section:
    kind: str
    bank: float = 0
    width: float = NARROW
    profile: tuple = tuple(V_SECTION)
    depth: float = WALL
    gauge: float = GAUGE
    radius: float = TUBE

    def __post_init__(self):
        if self.kind not in ('floor', 'rail'):
            raise ValueError(f'unsupported port kind {self.kind}')
        values = (self.bank, self.width, self.depth, self.gauge, self.radius,
                  *(v for p in self.profile for v in p))
        if not all(math.isfinite(v) for v in values):
            raise ValueError('port section needs finite values')
        if self.width <= 0 or self.depth <= 0 or self.radius <= 0 or self.gauge <= 2*self.radius:
            raise ValueError('invalid port width, depth or rail gauge/radius')
        if len(self.profile) < 2 or self.profile[0][0] != -.5 or self.profile[-1][0] != .5 or any(
                a[0] >= b[0] for a, b in zip(self.profile, self.profile[1:])):
            raise ValueError('port profile fractions must increase from -0.5 to 0.5')

    @property
    def standard(self):
        if abs(self.bank) > EPSILON:
            return False
        if self.kind == 'rail':
            return abs(self.gauge-GAUGE) <= EPSILON and abs(self.radius-TUBE) <= EPSILON
        return (abs(self.width-NARROW) <= EPSILON and abs(self.depth-WALL) <= EPSILON
                and same_profile(self.profile, V_SECTION))


@dataclass(frozen=True)
class Port:
    owner: str
    name: str
    position: tuple[float, float, float]
    heading: tuple[float, float]
    section: Section
    source: Origin | None = None
    distance: float = 0  # floor UV phase in world units, before division by 5

    def __post_init__(self):
        if not all(math.isfinite(v) for v in (*self.position, *self.heading, self.distance)):
            raise ValueError('port pose needs finite values')
        if abs(math.hypot(*self.heading)-1) > EPSILON:
            raise ValueError('port heading must be a unit direction')

    def data(self):
        value = asdict(self)
        ring = Ring(*self.position, *self.heading, self.section.bank)
        value['up'] = ring.axes()[1]
        return value


@dataclass(frozen=True)
class Connection:
    before: Port
    after: Port
    kind: str
    native: bool = False

    def data(self):
        return {'from': self.before.data(), 'to': self.after.data(), 'kind': self.kind,
                'caps': 'open', 'uv': 'native-grid' if self.native else
                'continue' if self.kind == 'floor-seam' else 'material-boundary'}


class ConnectionError(ValueError):
    def __init__(self, before, after, rule, remedy):
        self.before, self.after, self.rule = before, after, rule
        def label(port):
            return f'{port.owner}:{port.name}' + (f' at {port.source.pointer}' if port.source else '')
        super().__init__(f'{label(before)} -> {label(after)}: {rule}; {remedy}')


def connect(before: Port, after: Port, *, grid=False) -> Connection:
    """Require a closed positional joint and matching horizontal cross sections.

    Intentional slope changes remain allowed. Native module interiors, beam
    overlaps and ballistic transfers require their own adapters, not this rule.
    """
    def reject(rule, remedy='add an explicit connector before this piece'):
        raise ConnectionError(before, after, rule, remedy)
    if math.dist(before.position, after.position) > EPSILON:
        reject('position gap', 'align the two port positions')
    if math.dist(before.heading, after.heading) > EPSILON:
        reject('heading mismatch', 'use a curved path to align the end heading')
    a, b = before.section, after.section
    if grid and (cardinal(before.heading) is None or not a.standard):
        reject('grid connection needs a cardinal heading, zero bank and standard '
               '5-wide V floor or 3.8-gauge rail', 'add a free path transition or connector')
    if a.kind != b.kind:
        if not a.standard or not b.standard:
            reject('rail/floor transitions require standard ports')
        return Connection(before, after, 'rail-floor-dock', grid)
    fields = ('bank', 'width', 'depth') if a.kind == 'floor' else ('bank', 'gauge', 'radius')
    for field in fields:
        if abs(getattr(a, field)-getattr(b, field)) > EPSILON:
            reject(f'mismatched {field}')
    if a.kind == 'floor' and not same_profile(a.profile, b.profile):
        reject('mismatched section')
    return Connection(before, after, f'{a.kind}-seam', grid)


def incoming(layout, track) -> Port:
    if track.free_port is not None:
        return track.free_port
    previous = layout.parts[track.step] if track.step is not None else None
    origin = previous.origin if previous else None
    owner = origin.entity_id if origin else f'sector/{previous.sector}/start' if previous else 'route/start'
    return Port(owner, 'exit', (track.p[0], track.h, track.p[1]), track.d,
                Section('rail' if track.run is not None else 'floor', width=track.width), origin)


def entry(piece, track, section, *, owner=None) -> Port:
    origin = getattr(piece, 'origin', None)
    return Port(owner or (origin.entity_id if origin else 'route/end'), 'entry',
                (track.p[0], track.h, track.p[1]), track.d, section, origin)
