"""Editable source targets and transactional edits over resolved route geometry."""
from copy import deepcopy

from ballance.core.path import sample_path
from ballance.route.editing import FIELDS, fields_for, kind, leaves, validate_edit
from ballance.route.identity import pointer
from ballance.route.layout import Layout
from ballance.route.templates import expand


def locate(route, path):
    result = route
    for key in path.split('/')[1:]:
        key = key.replace('~1', '/').replace('~0', '~')
        result = result[int(key)] if isinstance(result, list) else result[key]
    return result


def replace_at(route, path, value):
    parent, _, key = path.rpartition('/')
    owner = locate(route, parent)
    key = key.replace('~1', '/').replace('~0', '~')
    owner[int(key) if isinstance(owner, list) else key] = deepcopy(value)


def model(route):
    layout = Layout(route)
    resolved = {p.origin.entity_id: p for p in leaves(expand(route))}
    entities, connections = layout.source_map()['entities'], layout.connection_report()
    entries = {c['to']['owner']: c['to'] for c in connections['connections']}
    for entity, part in zip(entities, layout.parts):
        piece = resolved.get(entity['id'])
        entity['effective'] = piece
        entity['geometry_kind'] = kind(piece) if piece else None
        entity['category'] = 'geometry' if entity['geometry_kind'] in FIELDS else 'resource'
        entity['path'] = [[a.x, a.y, a.z] for a, b in part.segments]
        if part.segments:
            b = part.segments[-1][1]
            entity['path'].append([b.x, b.y, b.z])
        entry = entity.get('entry') or entries.get(entity['id'])
        if not entity['path'] and entry:
            entity['path'] = [entry['position'], entity['end']['position']]
            if piece and 'floor_path' in piece:
                x, y, z = entry['position']; dx, dz = entry['heading']
                entity['path'] = [[x + s.point[0]*dx + s.point[1]*dz, y + s.point[2],
                                   z + s.point[0]*dz - s.point[1]*dx]
                                  for s in sample_path(piece['floor_path'])]
        entity['frame'] = {'position': entry['position'], 'heading': entry['heading']} if entry and entity['path'] else None
        source, targets = entity['source'], {}
        if source and source['stable'] and entity['geometry_kind']:
            raw = locate(route, source['pointer'])
            if source['instances']:
                targets['instance'] = {'piece': piece, 'pointer': f"/overrides/{pointer(entity['id'])}",
                                       'label': '仅此展开元素'}
                call = source['instances'][0]
                targets['call'] = {'piece': locate(route, call['pointer']), 'pointer': call['pointer'],
                                   'label': '此模板调用（含所有重复）'}
                template = source['instances'][-1]['template']
                definition = route['definitions'][template]
                if isinstance(definition, dict) and definition.get('parameters'):
                    targets['template'] = {'piece': {'parameters': definition['parameters']},
                        'pointer': f'/definitions/{pointer(template)}', 'label': f'共享模板默认值：{template}'}
            elif kind(raw):
                targets['piece'] = {'piece': raw, 'pointer': source['pointer'], 'label': '此路线元素'}
        entity['edit_targets'] = targets
        entity['editable'] = bool(targets)
        entity['overridden'] = entity['id'] in route.get('overrides', {})
        entity['piece'] = next(iter(targets.values()))['piece'] if targets else piece
    from .spatial import extend
    return extend({'free_layout':bool(route.get('placements')), 'entities': entities, 'connections': connections, 'sectors': len(layout.sectors)}, route, layout)


def edited_route(route, entity, piece, scope=None, reset=False):
    targets = entity['edit_targets']
    scope = scope or next(iter(targets), None)
    if scope not in targets:
        raise ValueError('Select an editable geometry element and a valid edit scope')
    target = targets[scope]
    candidate = deepcopy(route)
    original = target['piece']
    if reset and scope != 'instance':
        raise ValueError('Only an instance override can be reset')
    if scope in ('settings', 'placement', 'rule', 'mechanism'):
        from .spatial import edit_setting
        edit_setting(candidate, entity, piece, scope, target)
    elif scope in ('piece', 'instance'):
        if not reset:
            validate_edit(original, piece)
        if scope == 'piece':
            replace_at(candidate, target['pointer'], piece)
        else:
            overrides = candidate.setdefault('overrides', {})
            overrides.pop(entity['id'], None)
            if not reset:
                base = next(p for p in leaves(expand(candidate)) if p.origin.entity_id == entity['id'])
                assigned = {k: deepcopy(piece[k]) for k in fields_for(base)
                            if k in piece and (k not in base or piece[k] != base[k])}
                removed = sorted(k for k in fields_for(base) if k in base and k not in piece)
                if assigned or removed:
                    overrides[entity['id']] = {'set': assigned, 'remove': removed}
            if not overrides:
                candidate.pop('overrides', None)
    elif scope == 'call':
        if not isinstance(piece, dict):
            raise ValueError('Template call must be an object')
        changed = {k for k in original.keys() | piece.keys() if original.get(k) != piece.get(k)
                   or (k in original) != (k in piece)}
        if changed - {'with', 'repeat', 'mirror'}:
            raise ValueError('Only with, repeat and mirror can change; keep template call identity')
        replace_at(candidate, target['pointer'], piece)
    else:
        if (not isinstance(piece, dict) or set(piece) != {'parameters'}
                or not isinstance(piece['parameters'], dict)
                or set(piece['parameters']) != set(original['parameters'])):
            raise ValueError('Keep shared template parameter names unchanged')
        locate(candidate, target['pointer'])['parameters'] = deepcopy(piece['parameters'])
    return candidate, scope, target['pointer']


def impact(previous, after, entity_id, scope, pointer):
    old = {e['id']: e for e in previous['entities']}
    new = {e['id']: e for e in after['entities']}
    # Source and edit-target metadata do not imply a geometry change. Explicit
    # call parameters may shield an instance from a changed shared default.
    def geometry(e):
        return {k: e.get(k) for k in ('effective', 'path', 'end', 'after', 'sector', 'details')}
    added, removed = sorted(new.keys() - old.keys()), sorted(old.keys() - new.keys())
    changed = [key for key in new.keys() & old.keys() if geometry(new[key]) != geometry(old[key])]
    mesh_entities = [new[k] for k in added + changed] + [old[k] for k in removed]
    affected = [e['sector'] for e in mesh_entities if e.get('category') in ('geometry', 'resource')
                and not e.get('edit_owner')]
    return {'entity': entity_id, 'scope': scope, 'pointer': pointer,
            'changed_entities': sorted(changed), 'added_entities': added, 'removed_entities': removed,
            'rebuild_candidates': list(range(max(1, min(affected)), after['sectors'] + 1)) if affected else [],
            'global_checks': ['connections', 'scenery', 'mechanisms', 'death_zones', 'gameplay'],
            'validation': 'Previous gameplay evidence becomes historical. Local play skips earlier sectors.'}
