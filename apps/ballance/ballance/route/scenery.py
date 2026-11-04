"""The scenery around a laid-out route, placed as the original levels place
it (see the scenery in ballance.game.measures): lantern towers beside the
start, the checkpoints and the balloon, and striped poles by the balloon,
none where it would stand on a floor."""

from __future__ import annotations

import math

from ..core.geometry import at
from ..core.obj import Mesh
from ..game.measures import (BALLOON_STICKS, BALLOON_TOWERS, CHECKPOINT_TOWERS, START_OPENING,
                             START_TOWERS, STICK_HALF, TOWER_HALF)
from ..kit.floors import CELL, RISE
from ..kit.architecture import bar
from .layout import Layout
from .clashes import part_boxes, _placed
from ..core.space import Space, Volume, bounds, inflate
from .spaces import route_space, part_id, SCENERY_MARGIN
from . import attachments


def on_floor(layout: Layout, x: float, z: float, half: float) -> bool:
    """Whether a square of half width half at (x, z) lies over a floor cell,
    or the start's or a checkpoint's floor."""
    for floors in (f for sector in layout.sectors for f in sector.floors):
        for i, j in floors.cells:
            cx, cz = floors.ox + (i + 0.5) * CELL, floors.oz + (j + 0.5) * CELL
            if abs(cx - x) < half + CELL / 2 and abs(cz - z) < half + CELL / 2:
                return True
    for marker in [(layout.start[0], 0.0, layout.start[1])] + [m for m, _ in layout.checkpoints]:
        if math.hypot(marker[0] - x, marker[2] - z) < START_OPENING + half + 2:
            return True
    return False


def towers(layout: Layout) -> list[list[float]]:
    """Where the lantern towers stand: the tops of the pillars they stand on."""
    spots = [(layout.start, layout.marker_y, layout.heading, START_TOWERS, 1, 'marker/start')]
    for i, (marker, d) in enumerate(layout.checkpoints):
        spots.append(((marker[0], marker[2]), marker[1], d, CHECKPOINT_TOWERS, 1, f'marker/checkpoint/{i}'))
    if layout.balloon is not None:
        where, d = layout.balloon
        spots.append(((where[0], where[2]), where[1], d, BALLOON_TOWERS, -1, 'marker/finish'))
    out = []
    layout.lamp_anchors = {}
    for (px, pz), y, d, placed, sign, parent in spots:
        for i, (a, c, up) in enumerate(placed):
            x, z = at((px, pz), d, sign * a, c)
            if not on_floor(layout, x, z, TOWER_HALF):
                out.append([x, y + up, z])
                layout.lamp_anchors[(x, y+up, z)] = (f'{parent}/lamp/{i}', parent)
    return out


def sticks(layout: Layout, offsets: list[list[float]] | None = None) -> list[list[float]]:
    """Where the balloon's striped poles stand: their tops' middles."""
    if offsets is not None:
        if not isinstance(offsets, list) or any(
                not isinstance(p, list) or len(p) != 3 or any(
                    isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v)
                    for v in p) for p in offsets):
            raise ValueError('finish_poles needs [back, right, up] offsets from the balloon')
    if layout.balloon is None:
        return []
    where, d = layout.balloon
    out = []
    for a, c, up in BALLOON_STICKS if offsets is None else offsets:
        x, z = at((where[0], where[2]), d, -a, c)
        if offsets is not None or not on_floor(layout, x, z, STICK_HALF):
            out.append([x, where[1] + up, z])
    return out


class _Clearance:
    def __init__(self, layout, assets):
        self.space = route_space(layout)
        self.occupied = Space()
        self.solid_by_part = {part_id(part): part_boxes(part, assets)[0] for part in layout.parts}
        self.assets, self.meshes = assets, {}

    def boxes(self, position, kinds, turn=0):
        result = []
        for kind in kinds:
            key = kind, turn
            if key not in self.meshes:
                box = _placed(self.assets.out / self.assets.mesh(kind), [0, 0, 0], turn)
                if box is None:
                    raise ValueError(f'{kind}: empty scenery mesh')
                self.meshes[key] = box
            result.append(tuple(v+position[k//2] for k, v in enumerate(self.meshes[key])))
        return result

    def conflicts(self, boxes, owner):
        return (self.space.query(boxes, margin=SCENERY_MARGIN, first=True) or
                self.occupied.query(boxes, ignore=(owner,), first=True))

    def reserve(self, actor, boxes):
        self.occupied.remove(actor['id'])
        for box in boxes:
            self.occupied.add(Volume(actor['id'], 'scenery separation', box,
                                     source={'source': actor['source']}))


def fit(layout, assets, lamps, poles, rules=None):
    """Fit bounded assemblies while preserving their declared parent.

    Side braces and omission require explicit per-accessory permission. All
    outputs, even unchanged objects, receive independently checkable bindings.
    """
    rules = {} if rules is None else rules
    if not isinstance(rules, dict):
        raise ValueError('scenery rules must be an ID-to-rule object')
    clearance = _Clearance(layout, assets)
    actors = attachments.candidates(layout, lamps, poles, clearance.solid_by_part, clearance.boxes, rules)
    if len(poles) == 2 and not attachments.symmetric(poles, layout.balloon):
        raise ValueError('finish pole pair must be symmetric on opposite sides of the balloon')
    changes, bindings, braces = [], [], Mesh()
    # Reserve every original assembly before moving any, then replace its
    # reservation when it moves. A search cannot displace an unchanged lamp.
    for actor in actors:
        clearance.reserve(actor, clearance.boxes(actor['anchor'], actor['kinds'], actor['turn']))
    for actor in actors:
        position, kinds, rule = actor['anchor'], actor['kinds'], actor['rule']
        original = clearance.boxes(position, kinds, actor['turn'])
        def attached(candidate):
            if actor['role'] == 'pillar' or rule['mode'] == 'attached':
                return attachments.supported(clearance.boxes(candidate, kinds, actor['turn'])[0], actor['regions'])
            return True
        conflicts = clearance.conflicts(original, actor['id'])
        # Authored lamps also enter through this path. Previously only the
        # automatic generator filtered original positions over floor cells;
        # fitting checked candidates after movement but trusted the anchor.
        if actor['role'] == 'lamp' and on_floor(layout, position[0], position[2], TOWER_HALF):
            conflicts = conflicts or [{'kind':'lamp over floor', 'owner':actor['parent']}]
        chosen, brace = None, None
        if not conflicts and attached(position):
            chosen = position
        elif rule['mode'] != 'locked':
            reach = math.floor(rule['max_shift']/2.5)
            offsets = sorted(((x*2.5, z*2.5) for x in range(-reach, reach+1)
                              for z in range(-reach, reach+1)
                              if math.hypot(x*2.5,z*2.5) <= rule['max_shift']+1e-8),
                             key=lambda p: (p[0]**2+p[1]**2, abs(p[0]), p[0], p[1]))
            for dx, dz in offsets:
                candidate = [position[0]+dx, position[1], position[2]+dz]
                if not attached(candidate):
                    continue
                if actor['role'] == 'lamp' and on_floor(layout, candidate[0], candidate[2], TOWER_HALF):
                    continue
                boxes = clearance.boxes(candidate, kinds, actor['turn'])
                if not clearance.conflicts(boxes, actor['id']):
                    chosen = candidate
                    break
            if chosen is None and rule['allow_brace']:
                # The anchor itself must still be under the owning platform.
                # A beam reaching toward an arbitrary old position is no attachment.
                if not attachments.supported(original[0], actor['regions']):
                    raise ValueError(f"{actor['id']}: brace anchor is not under parent {actor['parent']}")
                box = original[0]
                anchor = ((box[0]+box[1])/2, box[3]-.5, (box[4]+box[5])/2)
                solids = [b for values in clearance.solid_by_part.values() for b in values]
                for dx, dz in offsets:
                    candidate = [position[0]+dx, position[1], position[2]+dz]
                    boxes = clearance.boxes(candidate, kinds, actor['turn'])
                    if clearance.conflicts(boxes, actor['id']) or any(
                            all(min(b[k+1],f[k+1])-max(b[k],f[k]) > 1e-5 for k in (0,2,4))
                            for b in boxes for f in solids):
                        continue
                    ends = [(anchor[0]+dx, anchor[1]-drop, anchor[2]+dz) for drop in (0,4)]
                    brace_boxes = [inflate(bounds((anchor,end)), .5) for end in ends]
                    if clearance.conflicts(brace_boxes, actor['id']):
                        continue
                    first_face = braces.count // 3
                    for end in ends:
                        bar(braces, anchor, end, .5)
                    chosen, brace = candidate, {'anchor': anchor, 'ends': ends,
                                                'faces': [first_face, braces.count // 3]}
                    break
        if chosen is None and not rule['allow_omit']:
            cause = conflicts[0] if conflicts else {'kind': 'attachment', 'owner': actor['parent']}
            raise ValueError(f"{actor['id']} at {position}: cannot satisfy {cause['kind']} against "
                             f"{cause['owner']} within {rule['max_shift']} units; "
                             'adjust its rule or route, or explicitly allow a brace/omission')
        action = 'omitted' if chosen is None else 'braced' if brace else 'moved' if chosen != position else 'kept'
        binding = {**actor, 'kinds': list(kinds), 'position': chosen, 'brace': brace, 'action': action, 'objects': []}
        bindings.append(binding)
        if action != 'kept':
            changes.append({'id': actor['id'], 'kind': kinds[0], 'parent': actor['parent'],
                            'from': position, 'to': chosen, 'action': action,
                            'reason': conflicts[0]['kind'] if conflicts else 'attachment', 'conflicts': conflicts})
        clearance.occupied.remove(actor['id'])
        if chosen is not None:
            boxes = clearance.boxes(chosen, kinds, actor['turn'])
            if brace:
                boxes += [inflate(bounds((brace['anchor'],end)), .5) for end in brace['ends']]
            clearance.reserve(actor, boxes)
    lamps_out, pillars, poles_out, supports = [], [], [], []
    counts = {}
    for binding in bindings:
        p, role = binding['position'], binding['role']
        if p is None:
            continue
        if role == 'lamp':
            lamps_out.append(p)
            i = len(lamps_out)
            binding['objects'] = [f'Tower_{i:02d}', f'Floor_Columns_{i:02d}']
        elif role == 'pillar':
            pillars.append(p)
            binding['objects'] = [f'Floor_Columns_{len(lamps_out)+len(pillars):02d}']
        elif role == 'pole':
            poles_out.append(p)
            binding['objects'] = [f'Stick_{len(poles_out):02d}']
        else:
            kind = binding['kinds'][0]
            counts[kind] = counts.get(kind,0)+1
            supports.append((kind, binding['sector'], p, binding['turn']))
            binding['objects'] = [f"A{binding['sector']:02d}_{kind}_{counts[kind]:02d}"]
    layout.supports, layout.scenery_bindings = supports, bindings
    if braces.count:
        assets.meshes['Scenery_Braces'] = 'scenery_braces.obj'
        braces.write(assets.out / assets.mesh('Scenery_Braces'))
    return lamps_out, pillars, poles_out, changes
