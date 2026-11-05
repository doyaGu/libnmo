"""Expand typed, parameterized route templates without evaluating code."""

from __future__ import annotations

import copy
import re

from .identity import Origin, Piece, identify, pointer
from ..game.resources import catalog

PARAMETER = re.compile(r'\$\{([A-Za-z_][A-Za-z_0-9]*)\}\Z')


def resource_piece(item):
    """Resolve named resources before route adapters run, retaining author IDs."""
    item = copy.deepcopy(item)
    descriptions = catalog()
    if 'resource' in item:
        name = item.pop('resource')
        spec = descriptions.get(name) if isinstance(name, str) else None
        placement = spec.get('placement', {}) if spec else {}
        if 'module' not in placement and 'route' not in placement:
            raise ValueError(
                f'{name}: not a route resource; use its element or generated placement entry'
            )
        if set(item) - {'id', *placement['parameters']}:
            raise ValueError(f'{name}: accepts id and {placement["parameters"]}')
        if 'count' in item and 'count_range' in placement:
            count = item['count']
            low, high = placement['count_range']
            if type(count) is not int or not low <= count <= high:
                raise ValueError(f'{name}: count must be an integer within {low}..{high}')
        defaults = placement.get('route', {'module': placement.get('module')})
        for source, target in placement.get('argument', {}).items():
            if source in item:
                item[target] = item.pop(source)
        item = {**defaults, **item}
    for element in item.get('elements', []):
        if 'resource' not in element:
            continue
        name = element.pop('resource')
        spec = descriptions.get(name) if isinstance(name, str) else None
        kind = spec.get('placement', {}).get('element') if spec else None
        if not kind or 'kind' in element:
            raise ValueError(f'{name}: not an element resource or conflicts with kind')
        element['kind'] = kind
    return item


def substitute(value, parameters):
    if isinstance(value, str) and (match := PARAMETER.fullmatch(value)):
        name = match.group(1)
        if name not in parameters:
            raise ValueError(f'missing template parameter {name}')
        return copy.deepcopy(parameters[name])
    if isinstance(value, list):
        return [substitute(v, parameters) for v in value]
    if isinstance(value, dict):
        return {k: substitute(v, parameters) for k, v in value.items()}
    return value


def mirror(piece):
    """Reflect about the incoming heading, retaining local edge numbering."""
    result = copy.deepcopy(piece)
    opposite = {'left': 'right', 'right': 'left'}
    for key in ('turn', 'rail_turn', 'beam_turn', 'exit', 'side', 'hop', 'railings'):
        if isinstance(result.get(key), str):
            result[key] = opposite.get(result[key], result[key])
    for element in result.get('elements', []):
        element['at'][1] *= -1
    for block in result.get('blocks', []):
        block[1] *= -1
    for branch in result.get('branches', []):
        if 'side' in branch:
            branch['side'] = opposite.get(branch['side'], branch['side'])
        branch['route'] = [mirror(p) for p in branch['route']]
    for key in ('rail_path', 'beam_path', 'floor_path', 'connector'):
        if key not in result:
            continue
        spec = result[key]
        for segment in spec.get('segments', []):
            if 'line' in segment:
                segment['line'][1] *= -1
            elif 'bezier' in segment:
                for p in segment['bezier']:
                    p[1] *= -1
            elif 'arc' in segment:
                segment['arc']['angle'] *= -1
            elif 'wave' in segment:
                segment['wave']['amplitude'] = -segment['wave'].get('amplitude', 12)
            elif 'weave' in segment:
                segment['weave']['hand'] = -segment['weave'].get('hand', 1)
            elif 'eased_turn' in segment:
                segment['eased_turn']['angle'] = -segment['eased_turn'].get('angle', 90)
        if 'bank' in spec:
            if isinstance(spec['bank'], list):
                for p in spec['bank']:
                    p[1] *= -1
            else:
                spec['bank'] *= -1
        for section in ('section', 'end_section'):
            if isinstance(spec.get(section), list):
                spec[section] = [[-u, y] for u, y in reversed(spec[section])]
        if key == 'connector':
            endpoint = spec.get('endpoint', {})
            if 'offset' in endpoint:
                endpoint['offset'][1] *= -1
                if 'heading' in endpoint:
                    endpoint['heading'][1] *= -1
            target = spec.get('to', {})
            if 'bank' in target:
                target['bank'] *= -1
            if isinstance(target.get('section'), list):
                target['section'] = [[-u, y] for u, y in reversed(target['section'])]
    if 'floor_polygon' in result:
        spec = result['floor_polygon']
        extra = [p for hole in spec.get('holes', []) for p in hole]
        extra += [p for port in spec.get('ports', {}).values() for p in port.get('path', [])]
        for p in spec['outline'] + spec.get('path', []) + spec.get('supports', []) + extra:
            p[1] *= -1
    return result


def expand(route):
    version = route.get('schema_version', 0)
    if type(version) is not int or version not in (0, 1):
        raise ValueError(
            'unsupported route schema_version; supported: 1 (or legacy without version)'
        )
    definitions = route.get('definitions', {})
    if not isinstance(definitions, dict):
        raise ValueError('route definitions must be a name-to-template object')
    count = 0
    identities = set()

    def pieces(
        items,
        stack=(),
        path='/sectors',
        scope='route',
        instances=(),
        stable_parent=True,
        fallback='',
    ):
        nonlocal count
        if not isinstance(items, list):
            raise ValueError('route pieces must be a list')
        result = []
        for index, item in enumerate(items):
            if not isinstance(item, dict):
                raise ValueError('each route piece must be an object')
            source = f'{path}/{index}'
            entity_id, stable = identify(
                item, source, scope, f'{fallback}{index}', version, stable_parent
            )
            if entity_id in identities:
                raise ValueError(f'{source}: duplicate entity ID {entity_id}')
            identities.add(entity_id)
            if 'use' in item:
                if set(item) - {'use', 'with', 'repeat', 'mirror', 'id'}:
                    raise ValueError('template use accepts id, with, repeat and mirror')
                name = item['use']
                if not isinstance(name, str) or name not in definitions:
                    raise ValueError(f'unknown route template {name}')
                if name in stack or len(stack) >= 32:
                    raise ValueError(f'recursive route template: {" -> ".join((*stack, name))}')
                definition = definitions[name]
                if not isinstance(definition, dict) or set(definition) - {'parameters', 'pieces'}:
                    raise ValueError(f'template {name} needs parameters and pieces')
                defaults, supplied = definition.get('parameters', {}), item.get('with', {})
                if (
                    not isinstance(defaults, dict)
                    or not isinstance(supplied, dict)
                    or set(supplied) - set(defaults)
                ):
                    raise ValueError(f'unknown parameters for template {name}')
                parameters = defaults | supplied
                repeat = item.get('repeat', 1)
                if (
                    not isinstance(repeat, int)
                    or isinstance(repeat, bool)
                    or not 1 <= repeat <= 100
                ):
                    raise ValueError('template repeat must be an integer within 1..100')
                if not isinstance(item.get('mirror', False), bool):
                    raise ValueError('template mirror must be boolean')
                for repetition in range(repeat):
                    instance = {
                        'id': entity_id,
                        'pointer': source,
                        'template': name,
                        'repeat': repetition,
                        'mirror': item.get('mirror', False),
                        'parameters': copy.deepcopy(parameters),
                    }
                    expanded = pieces(
                        substitute(definition.get('pieces'), parameters),
                        (*stack, name),
                        f'/definitions/{pointer(name)}/pieces',
                        f'{entity_id}/repeat/{repetition}',
                        (*instances, instance),
                        stable,
                    )
                    result.extend(mirror(p) if item.get('mirror') else p for p in expanded)
            else:
                count += 1
                if count > 4096:
                    raise ValueError('expanded route exceeds 4096 pieces')
                item = resource_piece(item)
                branch_ids = set()
                for number, branch in enumerate(item.get('branches', [])):
                    branch_path = f'{source}/branches/{number}'
                    branch_id, branch_stable = identify(
                        branch, branch_path, f'{entity_id}/branches', str(number), version, stable
                    )
                    if branch_id in branch_ids:
                        raise ValueError(f'{branch_path}: duplicate branch ID {branch_id}')
                    branch_ids.add(branch_id)
                    branch['route'] = pieces(
                        branch['route'],
                        stack,
                        f'{branch_path}/route',
                        branch_id,
                        instances,
                        branch_stable,
                    )
                result.append(Piece(item, Origin(entity_id, source, instances, stable)))
        return result

    from .editing import apply_overrides

    expanded = [
        pieces(sector, path=f'/sectors/{index}', fallback=f'sector/{index}/')
        for index, sector in enumerate(route['sectors'])
    ]
    return apply_overrides(expanded, route.get('overrides', {}))
