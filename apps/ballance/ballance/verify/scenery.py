"""Validate exported placements with the same spatial rules as the builder.

Attachment checks use final object transforms and parent mesh surfaces, not
just the placement journal's claim that a column was kept or moved.
"""

from __future__ import annotations

import math

from ..core.space import Space, Volume, bounds
from ..kit.floors import RISE, WALL
from ..route.spaces import route_space, SCENERY_MARGIN
from ..route.attachments import supported, symmetric

SCENERY = ('Tower_', 'Floor_Columns_', 'Stick_', 'Support_Scenery')


def _surface_at(triangle, x, z):
    a,b,c = triangle.points
    denominator = (b[2]-c[2])*(a[0]-c[0])+(c[0]-b[0])*(a[2]-c[2])
    if abs(denominator) < 1e-10:
        return None
    u = ((b[2]-c[2])*(x-c[0])+(c[0]-b[0])*(z-c[2]))/denominator
    v = ((c[2]-a[2])*(x-c[0])+(a[0]-c[0])*(z-c[2]))/denominator
    w = 1-u-v
    return u*a[1]+v*b[1]+w*c[1] if min(u,v,w) >= -1e-5 else None


def _contact(box, binding, objects):
    if not supported(box, binding['regions']):
        return False
    x0,x1,_,top,z0,z1 = box
    probes = [(x,z) for x in (x0,x1) for z in (z0,z1)] + [((x0+x1)/2,(z0+z1)/2)]
    triangles = [t for name in binding['parent_objects'] if name in objects for t in objects[name].triangles]
    # Native floors often omit underside triangles. Their measured WALL
    # thickness defines that face; sample the real top so holes remain holes.
    return all(any(y is not None and (y-.02 <= top <= y+RISE+.02 or
                                     y-WALL-.02 <= top <= y-WALL+RISE+.02)
                   for y in (_surface_at(t,x,z) for t in triangles)) for x,z in probes)


def check(scene, layout, bindings=None):
    space = route_space(layout)
    found = []
    objects = {obj.name: obj for obj in scene}
    registered = {name for b in bindings or [] for name in b['objects']}
    for obj in scene:
        if not (obj.name in registered or obj.name.startswith(SCENERY) or '_FanTower_Pillar_' in obj.name) or not obj.triangles:
            continue
        boxes = [bounds(t.points) for t in obj.triangles] if obj.name == 'Support_Scenery' else [
            bounds([p for t in obj.triangles for p in t.points])]
        seen = set()
        for contact in space.query(boxes, margin=SCENERY_MARGIN if bindings is not None else 0):
            if contact['owner'] not in seen:
                found.append({'object': obj.name, **contact})
                seen.add(contact['owner'])
    if bindings is None:
        return found
    occupied, assemblies = Space(), []
    for binding in bindings:
        def problem(reason):
            found.append({'object': ', '.join(binding['objects']) or binding['id'],
                          'kind': 'attachment', 'rule': reason, 'owner': binding['parent'],
                          'entity_id': binding['id'], 'source': binding['source'],
                          'where': binding['position'] or binding['anchor'],
                          'sector': binding.get('sector',0), 'piece': 'scenery', 'branch': None})
        if binding['position'] is None:
            if not binding['rule']['allow_omit']:
                problem('omission was not permitted')
            continue
        names = binding['objects']
        if not names or any(name not in objects or not objects[name].triangles for name in names):
            problem('missing assembly object or mesh')
            continue
        members = [objects[name] for name in names]
        position = members[0].at
        if binding['role'] == 'lamp':
            from ..route.scenery import on_floor
            from ..game.measures import TOWER_HALF
            if on_floor(layout, position[0], position[2], TOWER_HALF):
                problem('lamp over floor')
        if any(math.dist(obj.at,position) > 1e-4 for obj in members):
            problem('lamp and supporting column no longer share their attachment frame')
        rule, anchor = binding['rule'], binding['anchor']
        if rule['mode'] == 'locked' and math.dist(position,anchor) > 1e-4:
            problem('locked placement moved')
        if abs(position[1]-anchor[1]) > 1e-4 or math.dist((position[0],position[2]),(anchor[0],anchor[2])) > rule['max_shift']+1e-4:
            problem('placement exceeds its allowed movement')
        if binding['role'] in ('pillar','module'):
            parents = binding['parent_objects']
            if not parents or any(name not in objects for name in parents):
                problem('owning platform or module is missing')
            elif any(math.dist(objects[name].at,expected)>1e-4 for name,expected in binding['parent_positions'].items()):
                problem('parent moved away from its attachment frame')
            box = bounds([p for t in members[0].triangles for p in t.points])
            brace = binding['brace']
            if brace:
                if not rule['allow_brace']:
                    problem('side brace was not permitted')
                original = tuple(v+anchor[k//2]-position[k//2] for k,v in enumerate(box))
                if not _contact(original,binding,objects):
                    problem('brace anchor has no contact with its owning platform')
                mesh = objects.get('Support_Scenery')
                faces = mesh.triangles[slice(*brace['faces'])] if mesh else []
                vertices = [p for t in faces for p in t.points]
                if not vertices or any(not any(math.dist(p,q) <= .51 for p in vertices)
                                       for q in [brace['anchor'],*brace['ends']]):
                    problem('brace does not reach its recorded attachment points')
                if any(abs(brace['ends'][0][k]-(box[2*k]+box[2*k+1])/2) > .02 for k in (0,2)):
                    problem('column moved away from the brace')
            elif binding['role'] == 'pillar' or rule['mode'] == 'attached':
                if not _contact(box,binding,objects):
                    problem('capital has no contact with its owning platform')
        boxes = [bounds([p for t in obj.triangles for p in t.points]) for obj in members]
        if binding['brace'] and 'Support_Scenery' in objects:
            boxes += [bounds(t.points) for t in objects['Support_Scenery'].triangles[slice(*binding['brace']['faces'])]]
        assemblies.append((binding,boxes))
        for box in boxes:
            occupied.add(Volume(binding['id'],'scenery separation',box))
    for binding,boxes in assemblies:
        for contact in occupied.query(boxes,ignore=(binding['id'],)):
            found.append({'object': ', '.join(binding['objects']), 'sector': binding.get('sector',0),
                          'piece': 'scenery', 'branch': None, 'entity_id': binding['id'], **contact})
    poles = [objects[b['objects'][0]].at for b in bindings if b['role']=='pole' and b['objects'] and b['objects'][0] in objects]
    balloon = (objects['PE_Balloon_01'].at,layout.balloon[1]) if 'PE_Balloon_01' in objects else layout.balloon
    if len([b for b in bindings if b['role']=='pole']) == 2 and not symmetric(poles,balloon):
        found.append({'object':'finish poles','sector':len(layout.sectors),'piece':'finish','branch':None,
                      'kind':'attachment','rule':'finish pair is not symmetric about the balloon',
                      'where':layout.balloon[0]})
    return found
