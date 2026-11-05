"""Translate route rails, ball clearance and finish motion into spatial rules."""

import math

from ..core.space import Space, Volume, bounds, inflate
from ..game.measures import BALL_RADIUS
from .departure import boxes as departure_boxes

SCENERY_MARGIN = 0.5


def part_id(part):
    return part.origin.entity_id if part.origin else f'sector/{part.sector}/{part.what}/{part.step}'


def route_space(layout, mechanisms=True):
    space = Space()
    # Put finish motion first so it has a consistent diagnostic priority.
    for box in departure_boxes(layout.balloon):
        space.add(
            Volume(
                'finish/balloon',
                'balloon departure',
                box,
                'finish',
                source={
                    'sector': len(layout.sectors),
                    'piece': 'finish',
                    'branch': None,
                    'route_piece': 'balloon',
                },
            )
        )
    for part in layout.parts:
        source = {
            'sector': part.sector,
            'piece': part.index,
            'branch': part.branch,
            'route_piece': part.what,
            'entity_id': part_id(part),
            'source': part.origin.data() if part.origin else None,
        }
        for a, b in part.segments:
            for side in (0,) if a.gauge == b.gauge == 0 else (-1, 1):
                ends = tuple(
                    tuple(v + side * r.gauge / 2 * d for v, d in zip((r.x, r.y, r.z), r.axes()[0]))
                    for r in (a, b)
                )
                radius = max(a.radius, b.radius)
                space.add(
                    Volume(
                        part_id(part),
                        'rail',
                        inflate(bounds(ends), radius),
                        segment=ends,
                        radius=radius,
                        source=source,
                    )
                )
            centres = []
            for r in (a, b):
                height = math.sqrt(max(0, (BALL_RADIUS + r.radius) ** 2 - (r.gauge / 2) ** 2))
                centres.append(tuple(v + height * d for v, d in zip((r.x, r.y, r.z), r.axes()[1])))
            space.add(
                Volume(
                    part_id(part),
                    'ball clearance',
                    inflate(bounds(centres), BALL_RADIUS),
                    segment=tuple(centres),
                    radius=BALL_RADIUS,
                    source=source,
                )
            )
    if mechanisms:
        from .motions import add_to

        add_to(space, layout)
    return space
