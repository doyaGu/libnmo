"""Compile authored passage conditions and native-message dependencies."""

import math
import re

from ..game.measures import BALL_RADIUS
from .spaces import part_id

MESSAGES = {'Checkpoint reached', 'Level_Finish', 'Ball Off', 'Life_Up', 'Extrapoint'}


def compile_contracts(route, layout):
    settings = route.get('gameplay', {})
    if not isinstance(settings, dict) or set(settings) - {'contracts'}:
        raise ValueError('gameplay accepts contracts')
    contracts = settings.get('contracts', [])
    if not isinstance(contracts, list):
        raise ValueError('gameplay contracts must be a list')
    parts = {part_id(p): p for p in layout.parts}
    result = []
    known = {}
    for index, item in enumerate(contracts):
        source = f'/gameplay/contracts/{index}'
        if not isinstance(item, dict) or set(item) - {'id', 'at', 'ball', 'radius', 'event', 'sector', 'after'}:
            raise ValueError(f'{source}: unknown gameplay contract fields')
        identity = item.get('id')
        if not isinstance(identity, str) or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_.-]*', identity) or identity in known:
            raise ValueError(f'{source}: contract needs a unique stable id')
        if ('at' in item) == ('event' in item):
            raise ValueError(f'{source}: choose at or event')
        contract = {'id': identity, 'source': source}
        if 'at' in item:
            entity = item['at']
            part = parts.get(entity) if isinstance(entity, str) else None
            if part is None or part.end is None or 'sector' in item:
                raise ValueError(f'{source}: at must resolve to a route endpoint; sector is derived')
            ball = item.get('ball')
            radius = item.get('radius', 4)
            if ball not in ('Wood', 'Stone', 'Paper') or isinstance(radius, bool) or not isinstance(radius, (int, float)) or not math.isfinite(radius) or not 0 < radius <= 10:
                raise ValueError(f'{source}: ball needs Wood/Stone/Paper and radius must be in (0, 10]')
            (x, z), height, _ = part.end
            contract.update(kind='passage', entity=entity, ball=ball, radius=radius,
                            position=[x, height+BALL_RADIUS, z], sector=part.sector,
                            entity_source=part.origin.data() if part.origin else None)
        else:
            sector = item.get('sector')
            if not isinstance(item['event'], str) or item['event'] not in MESSAGES or type(sector) is not int or not 1 <= sector <= len(layout.sectors) or set(item) & {'ball', 'radius'}:
                raise ValueError(f'{source}: event needs a supported native message and valid sector')
            contract.update(kind='message', event=item['event'], sector=sector)
        after = item.get('after', [])
        if not isinstance(after, list) or any(not isinstance(dep, str) or dep not in known for dep in after) or len(set(after)) != len(after):
            raise ValueError(f'{source}: after must name distinct earlier contracts')
        if any(known[dep]['sector'] > contract['sector'] for dep in after):
            raise ValueError(f'{source}: dependencies cannot come from a later sector')
        contract['after'] = after
        result.append(contract)
        known[identity] = contract
    return {'schema_version': 1, 'contracts': result,
            'scope': 'Observed ball identity at authored endpoint gates and native All_Gameplay messages. Passage is not proof of mechanism completion.'}
