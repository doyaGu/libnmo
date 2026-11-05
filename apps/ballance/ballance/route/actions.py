"""Resolve authored action predicates to native instances and reference poses."""

import math

from ..core.motion import place
from ..core.path import number
from . import motions


def _box(value):
    if not isinstance(value, list) or len(value) != 6:
        raise ValueError('action region needs [xmin,xmax,ymin,ymax,zmin,zmax]')
    box = [number(v, 'action region') for v in value]
    if any(box[k] >= box[k + 1] for k in (0, 2, 4)):
        raise ValueError('action region bounds must increase')
    return box


def compile_action(spec, layout):
    if not isinstance(spec, dict) or set(spec) - {
        'mechanism',
        'rotation',
        'ball_region',
        'hold',
        'rise',
    }:
        raise ValueError('action accepts mechanism, rotation or ball_region, hold and rise')
    if ('rotation' in spec) == ('ball_region' in spec):
        raise ValueError('action needs exactly one rotation or ball_region condition')
    identifier = spec.get('mechanism')
    instance = next((item for item in motions.instances(layout) if item['id'] == identifier), None)
    if instance is None:
        raise ValueError(f'action mechanism not found: {identifier}')
    hold = spec.get('hold', 3)
    if type(hold) is not int or not 1 <= hold <= 600:
        raise ValueError('action hold must be 1..600 consecutive frames')
    result = {
        'mechanism': identifier,
        'instance': instance,
        'hold': hold,
        'sector': instance['sector'],
    }
    if 'ball_region' in spec:
        result.update(type='ball_region', box=_box(spec['ball_region']))
        if 'rise' in spec:
            rise = spec['rise']
            if not isinstance(rise, dict) or set(rise) != {'from', 'min_height', 'within'}:
                raise ValueError('rise needs a from region, min_height and within frames')
            height = number(rise['min_height'], 'minimum rise')
            if height <= 0 or type(rise['within']) is not int or not 1 <= rise['within'] <= 3600:
                raise ValueError('rise needs positive height and 1..3600 frames')
            result['rise'] = {
                'from': _box(rise['from']),
                'min_height': height,
                'within': rise['within'],
            }
        return result
    if 'rise' in spec:
        raise ValueError('rise is only valid with a ball_region condition')
    rotation = spec['rotation']
    recipe = motions.catalog().get(instance['kind'], {})
    if (
        not isinstance(rotation, dict)
        or set(rotation) - {'body', 'min_degrees', 'direction'}
        or not isinstance(rotation.get('body'), str)
    ):
        raise ValueError('rotation needs body, min_degrees and optional direction')
    body = recipe.get('bodies', {}).get(rotation['body'])
    if body is None or recipe.get('type') != 'hinge':
        raise ValueError('rotation requires a described native hinge body')
    degrees = number(rotation.get('min_degrees'), 'minimum rotation')
    direction = rotation.get('direction', 'either')
    if direction not in ('positive', 'negative', 'either'):
        raise ValueError('rotation direction must be positive, negative or either')
    if not 0 < degrees <= 180:
        raise ValueError('minimum rotation must be in (0,180]')
    matrix = body['matrix']
    reference = []
    for offset in (0, 4, 8):
        reference.extend((*place(matrix[offset : offset + 3], (0, 0, 0), instance['yaw']), 0))
    reference.extend((*place(matrix[12:15], instance['position'], instance['yaw']), 1))
    if not all(math.isfinite(v) for v in reference):
        raise ValueError('invalid action reference pose')
    return result | {
        'type': 'rotation',
        'body': rotation['body'],
        'min_degrees': degrees,
        'reference': reference,
        'axis': place(recipe['axis'], (0, 0, 0), instance['yaw']),
        'direction': direction,
    }
