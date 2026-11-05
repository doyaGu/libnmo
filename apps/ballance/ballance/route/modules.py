"""Measured route layouts and catalog-owned native component geometry."""

from __future__ import annotations

from ..core.obj import Box
from ..game.resources import catalog
from ..game.measures import DRAWBRIDGE_FLOORS, DRAWBRIDGE_HINGE, SACK_STEP, SLAB, SLAB_GAP

MODULES = {}
NEEDS_BALL = {
    'Trough': (
        {'Wood'},
        "can't be counted on to swing over into the next trough (on Level_04's, "
        'a stone ball never does and a paper one only sometimes); a wood ball does',
    )
}


def module_layout(kind: str, count: int) -> tuple[float, list[float], bool]:
    """The length between a module's funnels, where each module stands from
    their start, and whether the funnels end in walls (closing them where
    they stop at a gap)."""
    if kind == 'P_Modul_25':
        return DRAWBRIDGE_FLOORS, [DRAWBRIDGE_HINGE], False
    if kind == 'P_Modul_26':
        return SACK_STEP * count, [SACK_STEP * (k + 0.5) for k in range(count)], False
    step = SLAB + SLAB_GAP
    return step * count + SLAB_GAP, [SLAB_GAP + SLAB / 2 + step * k for k in range(count)], True


class ModuleFloor:
    """A floor cut out of an original level for a module, placed and turned
    as the module: its mesh's name (ballance_level_elements.py writes
    <name>.obj), its groups, and the boxes (x0, x1, y0, y1, z0, z1 in the
    module's frame) that stand in for it without the game's meshes."""

    def __init__(
        self,
        name: str,
        groups: list[str],
        boxes: list[Box],
        visible: bool = True,
        support: bool = False,
    ) -> None:
        self.name, self.groups, self.boxes, self.visible = name, groups, boxes, visible
        self.support = support


MODULE_FLOORS = {}
for _name, _spec in catalog().items():
    _placement = _spec.get('placement', {})
    _kind = _placement.get('module') or _placement.get('module_floor')
    if _kind:
        MODULE_FLOORS[_kind] = [
            ModuleFloor(
                p['name'],
                p['groups'],
                [tuple(b) for b in p['fallback_boxes']],
                visible=p['visible'],
                support=p['support'],
            )
            for p in _spec['parts']
            if 'fallback_boxes' in p
        ]
        if 'ball_warning' in _spec:
            NEEDS_BALL[_kind] = (set(_spec['needs_ball']), _spec['ball_warning'])
    if 'module' in _placement:
        MODULES[_kind] = tuple(_placement['count_range'])
