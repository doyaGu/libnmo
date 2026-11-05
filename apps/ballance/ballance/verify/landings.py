"""Confirm declared lower-layer landings in a fresh native ball trace."""

import math

from .actions import ActionTrace
from .play import steer_from
from ..route.spaces import part_id


def observe(passage, rows):
    departure = passage['points'][0]
    landing = passage['points'][-1]
    radius = max(0.5, passage['landing_radius'])
    started, previous, held = None, None, 0
    for row in rows:
        point, frame = row['position'], row['frame']
        continuous = previous is not None and (
            frame == previous['frame'] + 1
            and row['id'] == previous['id']
            and row['name'] == previous['name']
            and math.dist(point, previous['position']) < 20
        )
        if not continuous:
            started, held = None, 0
        if math.dist(point, departure) <= 2:
            started = row
        if started and frame - started['frame'] > 600:
            started = None
        arrived = (
            started is not None
            and frame > started['frame']
            and math.hypot(point[0] - landing[0], point[2] - landing[2]) <= radius
            and abs(point[1] - landing[1]) <= 0.5
            and point[1] < started['position'][1] - 1
        )
        held = held + 1 if arrived else 0
        if held >= 3:
            return {
                'status': 'passed',
                'departure_frame': started['frame'],
                'landing_frame': frame - 2,
                'confirmed_frame': frame,
                'ball': row['name'],
                'runtime_id': row['id'],
            }
        previous = row
    return {
        'status': 'failed',
        'reason': 'No continuous departure followed by three samples '
        'in the declared landing disc and layer.',
    }


def inspect(folder, layout, sector=1, finish=None):
    rows = [
        row
        for row in ActionTrace(folder, []).ball
        if row['frame'] >= steer_from(sector) and (finish is None or row['frame'] <= finish)
    ]
    sectors = {part_id(part): part.sector for part in layout.parts}
    results = []
    for passage in layout.passages:
        if passage['kind'] != 'landing' or sectors[passage['owner']] < sector:
            continue
        results.append({'id': passage['id'], 'owner': passage['owner'], **observe(passage, rows)})
    return {
        'status': 'passed' if all(r['status'] == 'passed' for r in results) else 'failed',
        'landings': results,
        'scope': 'Observed ball identity and continuous frames from departure to the '
        'declared landing disc and layer. Three arrival samples confirm proximity, '
        'not solver contact or all possible launch speeds.',
    }
