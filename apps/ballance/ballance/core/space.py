"""Deterministic spatial queries for conservative, state-aware constraints.

XZ buckets are only a broad phase. Candidates are tested against a box or
a centre segment swept through an inflated box (a conservative sphere bound).
"""

from dataclasses import asdict, dataclass, field
import math


def bounds(points):
    return tuple(
        v for k in range(3) for v in (min(p[k] for p in points), max(p[k] for p in points))
    )


def overlap(a, b):
    return all(min(a[k + 1], b[k + 1]) >= max(a[k], b[k]) - 1e-8 for k in (0, 2, 4))


def inflate(box, margin):
    return tuple(v + (-margin if k % 2 == 0 else margin) for k, v in enumerate(box))


def sweep_hit(a, b, box, radius):
    lo_t, hi_t = 0.0, 1.0
    for k in range(3):
        d = b[k] - a[k]
        lo, hi = box[2 * k] - radius, box[2 * k + 1] + radius
        if abs(d) < 1e-10:
            if not lo <= a[k] <= hi:
                return None
        else:
            first, last = sorted(((lo - a[k]) / d, (hi - a[k]) / d))
            lo_t, hi_t = max(lo_t, first), min(hi_t, last)
            if lo_t > hi_t:
                return None
    return tuple(a[k] + (b[k] - a[k]) * lo_t for k in range(3))


@dataclass(frozen=True)
class Volume:
    owner: str
    rule: str
    box: tuple
    state: str = 'always'
    segment: tuple | None = None
    radius: float = 0
    source: dict = field(default_factory=dict)
    severity: str = 'error'
    centre: tuple | None = None  # a sphere, with box used only for broad phase

    def __post_init__(self):
        if (
            len(self.box) != 6
            or not all(math.isfinite(v) for v in self.box)
            or any(self.box[k] > self.box[k + 1] for k in (0, 2, 4))
        ):
            raise ValueError('spatial bounds must be finite ordered pairs')
        if not math.isfinite(self.radius) or self.radius < 0:
            raise ValueError('spatial radius must be finite and nonnegative')
        if self.centre is not None:
            if (
                self.segment is not None
                or len(self.centre) != 3
                or not all(math.isfinite(v) for v in self.centre)
            ):
                raise ValueError('spatial sphere needs one finite centre and no segment')
            if any(
                self.box[2 * k] > v - self.radius + 1e-8
                or self.box[2 * k + 1] < v + self.radius - 1e-8
                for k, v in enumerate(self.centre)
            ):
                raise ValueError('spatial bounds do not contain their sphere')
        if self.segment is not None:
            if len(self.segment) != 2 or any(
                len(p) != 3 or not all(math.isfinite(v) for v in p) for p in self.segment
            ):
                raise ValueError('spatial sweep needs two finite points')
            required = inflate(bounds(self.segment), self.radius)
            if any(
                self.box[k] > required[k] + 1e-8 or self.box[k + 1] < required[k + 1] - 1e-8
                for k in (0, 2, 4)
            ):
                raise ValueError('spatial bounds do not contain their sweep')


class Space:
    def __init__(self):
        self.volumes = []
        self.buckets = {}
        self.inactive = set()

    @staticmethod
    def _cells(box):
        return (
            (x, z)
            for x in range(math.floor(box[0] / 25), math.floor(box[1] / 25) + 1)
            for z in range(math.floor(box[4] / 25), math.floor(box[5] / 25) + 1)
        )

    def add(self, volume: Volume):
        index = len(self.volumes)
        self.volumes.append(volume)
        for cell in self._cells(volume.box):
            self.buckets.setdefault(cell, []).append(index)

    def remove(self, owner):
        self.inactive.update(i for i, v in enumerate(self.volumes) if v.owner == owner)

    def query(self, boxes, *, states=None, ignore=(), margin=0, first=False):
        """Return contacts with owners, rules, states and source locations.

        A Volume can also be queried; spherical probes retain their shape
        against boxes, spheres and swept rail segments. states=None checks every state,
        including finish-only motion. Ignoring
        an assembly owner permits its own lamp and support to touch each other.
        """
        if not math.isfinite(margin) or margin < 0:
            raise ValueError('spatial margin must be finite and nonnegative')
        probe = boxes if isinstance(boxes, Volume) else None
        if probe is not None:
            boxes = [probe.box]
        found, seen = [], set()
        for box in boxes:
            box = inflate(box, margin)
            candidates = sorted(
                {i for cell in self._cells(box) for i in self.buckets.get(cell, [])}
            )
            for i in candidates:
                if i in self.inactive:
                    continue
                volume = self.volumes[i]
                key = volume.owner, volume.rule, volume.state, volume.source.get('passage')
                if (
                    key in seen
                    or volume.owner in ignore
                    or (
                        states is not None
                        and volume.state != 'always'
                        and volume.state not in states
                    )
                    or not overlap(box, volume.box)
                ):
                    continue
                where = (
                    sweep_hit(*volume.segment, box, volume.radius)
                    if volume.segment
                    else tuple(
                        (max(box[k], volume.box[k]) + min(box[k + 1], volume.box[k + 1])) / 2
                        for k in (0, 2, 4)
                    )
                )
                if volume.centre is not None:
                    where = tuple(
                        max(box[2 * k], min(v, box[2 * k + 1])) for k, v in enumerate(volume.centre)
                    )
                    if math.dist(where, volume.centre) > volume.radius:
                        where = None
                if where is not None and probe is not None and probe.centre is not None:
                    centre = probe.centre
                    other_radius = 0
                    if volume.segment:
                        a, b = volume.segment
                        length = sum((q - p) ** 2 for p, q in zip(a, b))
                        t = (
                            max(
                                0,
                                min(
                                    1,
                                    sum((v - p) * (q - p) for v, p, q in zip(centre, a, b))
                                    / length,
                                ),
                            )
                            if length
                            else 0
                        )
                        where = tuple(p + (q - p) * t for p, q in zip(a, b))
                        other_radius = volume.radius
                    elif volume.centre is not None:
                        where = volume.centre
                        other_radius = volume.radius
                    else:
                        where = tuple(
                            max(volume.box[2 * k], min(v, volume.box[2 * k + 1]))
                            for k, v in enumerate(centre)
                        )
                    if math.dist(where, centre) > probe.radius + margin + other_radius:
                        where = None
                if where is not None:
                    found.append(
                        {
                            'owner': volume.owner,
                            'kind': volume.rule,
                            'state': volume.state,
                            'severity': volume.severity,
                            'where': list(where),
                            'conservative': True,
                            **volume.source,
                        }
                    )
                    seen.add(key)
                    if first:
                        return found
        return found

    def data(self):
        return [asdict(v) for i, v in enumerate(self.volumes) if i not in self.inactive]
