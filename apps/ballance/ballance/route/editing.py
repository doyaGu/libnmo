"""Editable geometry fields and isolated overrides of expanded template leaves.

Overrides apply after substitution and mirroring, in the selected leaf's
incoming local frame. IDs, branch topology and template definitions stay intact.
"""

from copy import deepcopy
from dataclasses import replace
from .identity import pointer

FIELDS = {
    'connector': {'connector'},
    'rail_path': {'rail_path'},
    'beam_path': {'beam_path'},
    'floor_path': {'floor_path', 'elements'},
    'floor_polygon': {'floor_polygon', 'elements'},
    'road': {'road', 'width', 'drop', 'railings', 'elements'},
    'area': {'area', 'exit', 'rise', 'railings', 'blocks', 'elements'},
    'turn': {'turn', 'railing', 'elements'},
    'rail': {'rail', 'drop', 'hump', 'railings'},
    'rail_turn': {'rail_turn', 'hairpin', 'drop'},
    'beam': {'beam', 'drop', 'hump', 'sack', 'point'},
    'beam_turn': {'beam_turn', 'gap'},
}


EDIT_FIELDS = FIELDS | {
    'module': {'module', 'count', 'side', 'maze', 'elements'},
    'fan': {'fan', 'elements'},
    'tower': {'tower', 'elements'},
    'cradle': {'cradle', 'elements'},
    'trafo': {'trafo', 'elements'},
    'node': {'node', 'exit', 'elements'},
    'fall': {'fall', 'split', 'elements'},
    'log': {'log', 'climb', 'turn', 'drop', 'hop', 'elements'},
    'trough': {'trough', 'holes', 'climb', 'turn', 'drop', 'hop', 'elements'},
    'walkway': {'walkway', 'long', 'elements'},
    **{key: {key, 'elements'} for key in ('plank', 'twist', 'jump', 'spiral')},
}


def fields_for(piece):
    if 'resource' in piece:
        from ..game.resources import catalog

        name = piece['resource']
        placement = catalog().get(name, {}).get('placement', {}) if isinstance(name, str) else {}
        if 'route' not in placement and 'module' not in placement:
            raise ValueError('Select a route resource from the catalog')
        # Names and branch topology are referenced elsewhere; keep them stable.
        return {'resource', *placement.get('parameters', [])} - {'name', 'branches'}
    return EDIT_FIELDS[kind(piece)]


def kind(piece):
    if 'resource' in piece:
        return 'resource'
    found = [
        key
        for key in EDIT_FIELDS
        if key in piece and not (key == 'turn' and ('log' in piece or 'trough' in piece))
    ]
    return found[0] if len(found) == 1 else None


def validate_edit(original, edited):
    name = kind(original)
    if not isinstance(edited, dict) or not name or kind(edited) != name:
        raise ValueError('Keep the geometry kind unchanged')
    if edited.get('id') != original.get('id'):
        raise ValueError('Keep the element ID unchanged')
    changed = {
        key
        for key in original.keys() | edited.keys()
        if key not in original or key not in edited or original[key] != edited[key]
    }
    allowed = fields_for(original) | fields_for(edited)
    if changed - allowed:
        raise ValueError(
            f'{name}: editable fields are {sorted(allowed)}; identity and branches are locked'
        )


def leaves(sectors):
    for sector in sectors:
        for item in sector:
            yield item
            for branch in item.get('branches', []):
                yield from leaves([branch['route']])


def apply_overrides(sectors, overrides):
    if not isinstance(overrides, dict):
        raise ValueError('overrides must map stable template entity IDs to set/remove patches')
    by_id = {p.origin.entity_id: p for p in leaves(sectors)}
    for entity_id, patch in overrides.items():
        item = by_id.get(entity_id)
        if item is None:
            raise ValueError(
                f'orphan template override: {entity_id}; '
                'restore the instance or remove its override'
            )
        if not item.origin.stable or not item.origin.instances or not kind(item):
            raise ValueError(f'{entity_id}: overrides require a stable template geometry element')
        if (
            not isinstance(patch, dict)
            or set(patch) - {'set', 'remove'}
            or not isinstance(patch.get('set', {}), dict)
            or not isinstance(patch.get('remove', []), list)
            or any(not isinstance(k, str) for k in patch.get('remove', []))
        ):
            raise ValueError(f'{entity_id}: invalid override patch')
        assigned, removed = patch.get('set', {}), patch.get('remove', [])
        if set(assigned) & set(removed) or len(removed) != len(set(removed)):
            raise ValueError(f'{entity_id}: conflicting override fields')
        if (set(assigned) | set(removed)) - fields_for(item):
            raise ValueError(
                f'{entity_id}: override cannot change identity, branches or unknown fields'
            )
        # Keep expanded branch objects alive when parent and child both have overrides.
        edited = dict(item)
        for key in removed:
            edited.pop(key, None)
        edited.update(deepcopy(assigned))
        validate_edit(item, edited)
        if 'elements' in assigned:
            from .templates import resource_piece

            edited['elements'] = resource_piece({'elements': edited['elements']})['elements']
        item.clear()
        item.update(edited)
        item.origin = replace(item.origin, override_pointer=f'/overrides/{pointer(entity_id)}')
    return sectors
