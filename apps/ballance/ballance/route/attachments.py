"""Authored placement permissions and fixed parent ownership for scenery."""

import math

from ..kit.floors import RISE
from ..game.resources import resource, scenery_resource, catalog
from ..core.geometry import at
from .spaces import part_id
from .modules import MODULE_FLOORS


def symmetric(positions, balloon):
    if len(positions) != 2 or balloon is None:
        return False
    (x, y, z), (dx, dz) = balloon
    local = [((p[0]-x)*dx+(p[2]-z)*dz, (p[0]-x)*dz-(p[2]-z)*dx, p[1]-y) for p in positions]
    a, b = local
    return abs(a[0]-b[0]) < 1e-5 and abs(a[2]-b[2]) < 1e-5 and abs(a[1]+b[1]) < 1e-5 and a[1]*b[1] < 0


def supported(box, regions):
    x0, x1, _, top, z0, z1 = box
    candidates = [r for r in regions if r[2]-.02 <= top <= r[2]+RISE+.02 and
                  r[0] <= x1+.02 and r[1] >= x0-.02 and r[4] <= z1+.02 and r[5] >= z0-.02]
    # A triangulated platform has many adjacent regions. Its capital can
    # span several; require their union to cover the entire footprint.
    xs = sorted({x0,x1,*[max(x0,min(x1,r[k])) for r in candidates for k in (0,1)]})
    zs = sorted({z0,z1,*[max(z0,min(z1,r[k])) for r in candidates for k in (4,5)]})
    return bool(candidates) and all(any(r[0]-.02 <= (a+b)/2 <= r[1]+.02 and
                                       r[4]-.02 <= (c+d)/2 <= r[5]+.02 for r in candidates)
                                    for a,b in zip(xs,xs[1:]) for c,d in zip(zs,zs[1:]))


def policy(settings, identifier, mode, parent):
    value = settings.get(identifier, {})
    if not isinstance(value, dict) or set(value) - {'mode', 'max_shift', 'allow_brace', 'allow_omit', 'parent'}:
        raise ValueError(f'{identifier}: placement rule accepts mode, max_shift, allow_brace, allow_omit and parent')
    result = {'mode': mode, 'max_shift': 15., 'allow_brace': False, 'allow_omit': False,
              'parent': parent} | value
    if not isinstance(result['parent'], str) or not result['parent']:
        raise ValueError(f'{identifier}: parent must be an entity ID')
    if result['mode'] not in ('locked', 'nearby', 'attached'):
        raise ValueError(f'{identifier}: mode must be locked, nearby or attached')
    if mode != 'nearby' and result['mode'] == 'nearby':
        raise ValueError(f'{identifier}: a support cannot discard its attachment')
    if mode == 'nearby' and result['mode'] == 'attached':
        raise ValueError(f'{identifier}: lamps move as an assembly; use nearby or locked')
    if mode == 'nearby' and result['parent'] != parent:
        raise ValueError(f'{identifier}: cannot change a lamp assembly parent')
    if mode == 'locked' and result['parent'] != parent and result['mode'] == 'locked':
        raise ValueError(f'{identifier}: reparenting a native support requires attached mode')
    distance = result['max_shift']
    if isinstance(distance, bool) or not isinstance(distance, (int, float)) or not math.isfinite(distance) or not 0 <= distance <= 40:
        raise ValueError(f'{identifier}: max_shift must be within 0..40')
    if any(type(result[k]) is not bool for k in ('allow_brace', 'allow_omit')):
        raise ValueError(f'{identifier}: allow_brace and allow_omit must be boolean')
    if result['allow_brace'] and result['mode'] != 'attached':
        raise ValueError(f'{identifier}: a brace requires attached mode')
    return result


def candidates(layout, lamps, poles, solid_by_part, box_at, settings):
    """Assign stable accessory IDs before fitting, including omitted candidates."""
    actors = []
    parts = {part_id(p): p for p in layout.parts}
    for i, position in enumerate(lamps):
        identity, parent = getattr(layout, 'lamp_anchors', {}).get(tuple(position), (f'lamp/{i}', 'marker'))
        _, lamp = scenery_resource('lamp')
        actors.append({'id': identity, 'role': 'lamp', 'kinds': tuple(lamp['attachments']['assembly']['parts']),
                       'anchor': position, 'parent': parent, 'turn': 0,
                       'default': resource('Balloon')['attachments']['lamps']['mode'] if parent=='marker/finish' else lamp['placement']['mode']})
    counters = {}
    for position in layout.pillars:
        parent = next((part_id(p) for p in layout.parts if position in p.pillars), None)
        if parent is None:
            # Compatibility for programmatic layouts: bind once at the original
            # position, never search other floors after the column moves.
            parent = next((key for key, regions in solid_by_part.items()
                           if supported(box_at(position, ('Pillar',))[0], regions)), None)
        if parent is None:
            raise ValueError(f'Pillar at {position}: no parent platform supports its capital')
        index = counters.get(parent, 0)
        counters[parent] = index+1
        actors.append({'id': f'{parent}/pillar/{index}', 'role': 'pillar', 'kinds': ('Pillar',),
                       'anchor': position, 'parent': parent, 'turn': 0,
                       'default': scenery_resource('pillar')[1]['placement']['mode']})
    for kind, sector, position, turn in layout.supports:
        parent = next((part_id(p) for p in layout.parts
                       for module, _, where, rotation in p.module_floors
                       if where == position and rotation == turn and any(
                           f.name == kind and f.support for f in MODULE_FLOORS[module])), None)
        if parent is None:
            raise ValueError(f'{kind} at {position}: no owning native module; attach it explicitly in the route')
        actors.append({'id': f'{parent}/support/{kind}', 'role': 'module', 'kinds': (kind,),
                       'sector': sector, 'anchor': position, 'parent': parent, 'turn': turn,
                       'default': resource('FanTower')['attachments'].get(kind,{}).get('mode','locked')})
    actors.extend(authored(layout, parts))
    # Manifest numbering groups every lamp column before platform columns.
    actors.sort(key=lambda a: a['role'] != 'lamp')
    known = {a['id'] for a in actors}
    if set(settings)-known:
        raise ValueError(f'unknown scenery rule IDs: {sorted(set(settings)-known)}')
    for actor in actors:
        rule = policy(settings, actor['id'], actor.pop('default'), actor['parent'])
        parent = rule['parent']
        if actor['role'] != 'lamp' and parent not in parts:
            raise ValueError(f"{actor['id']}: unknown parent {parent}")
        actor['parent'], actor['rule'] = parent, rule
        actor['regions'] = solid_by_part.get(parent, [])
        actor['source'] = parts[parent].origin.data() if parent in parts and parts[parent].origin else None
    for i, position in enumerate(poles):
        actors.append({'id': f'finish/pole/{i}', 'role': 'pole', 'kinds': ('Stick',),
                       'anchor': position, 'parent': 'finish/balloon', 'turn': 0, 'regions': [], 'source': None,
                       'rule': {'mode': resource('Balloon')['attachments']['poles']['mode'], 'max_shift': 0., 'allow_brace': False,
                                'allow_omit': False, 'parent': 'finish/balloon'}})
    return actors


def authored(layout, parts):
    """Place native scenery relative to the owning piece's exit frame.

    at is [along, right, up], measured from its V-bottom datum. A lantern
    keeps its complete column; a column must meet its parent's underside.
    """
    values = getattr(layout, 'resource_scenery', [])
    if not isinstance(values, list):
        raise ValueError('scenery.instances must be a list')
    descriptions, actors, seen = catalog(), [], set()
    for value in values:
        if not isinstance(value, dict) or set(value) != {'id','resource','parent','at'}:
            raise ValueError('a scenery instance needs id, resource, parent and at')
        identifier, name, parent, offset = (value[k] for k in ('id','resource','parent','at'))
        if not isinstance(identifier, str) or not identifier or identifier in seen:
            raise ValueError('scenery instance IDs must be nonempty and unique')
        seen.add(identifier)
        spec = descriptions.get(name) if isinstance(name, str) else None
        if not spec or not spec.get('placement', {}).get('authorable'):
            raise ValueError(f'{name}: not an authorable scenery resource; finish poles use scenery.finish_poles')
        if not isinstance(parent, str) or parent not in parts:
            raise ValueError(f'{identifier}: unknown parent {parent}')
        if not isinstance(offset, list) or len(offset) != 3 or any(
                isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) for v in offset):
            raise ValueError(f'{identifier}: at needs finite [along, right, up] coordinates')
        p, h, heading = parts[parent].end
        x, z = at(p, heading, offset[0], offset[1])
        actors.append({'id':f'scenery/id/{identifier}', 'role':spec['placement']['role'],
                       'kinds':tuple(spec['attachments']['assembly']['parts']), 'parent':parent,
                       'anchor':[x,h+offset[2],z], 'turn':0, 'default':spec['placement']['mode']})
    return actors


def bind_objects(layout, manifest):
    """Link author identities to native names, without changing game naming."""
    objects = manifest['scenes'][0]['objects']
    parts = {part_id(p): p for p in layout.parts}
    for binding in layout.scenery_bindings:
        parent = parts.get(binding['parent'])
        names = []
        if parent:
            if parent.floors or parent.extras:
                names.append(f'A{parent.sector:02d}_Floor_02')
            for module, _, position, _ in parent.module_floors:
                for floor in MODULE_FLOORS[module]:
                    if floor.support:
                        continue
                    names.extend(o['name'] for o in objects if o.get('mesh', {}).get('obj') == f'{floor.name}.obj'
                                 and o.get('transform', {}).get('position') == position)
        binding['parent_objects'] = names
        binding['parent_positions'] = {o['name']: o.get('transform', {}).get('position', [0,0,0])
                                       for o in objects if o['name'] in names}
