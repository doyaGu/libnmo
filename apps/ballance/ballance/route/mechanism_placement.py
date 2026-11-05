"""Authored native-instance frames, separate from their supporting route geometry."""

from ..core.motion import place
from ..core.path import vector, number
from .spaces import part_id


def prepare(layout):
    settings = getattr(layout, 'mechanism_settings', {})
    if not isinstance(settings, dict) or set(settings) - {'interactions', 'instances'}:
        raise ValueError('mechanisms accepts interactions and instances only')
    overrides = settings.get('instances', {})
    if not isinstance(overrides, dict):
        raise ValueError('mechanism instances must be an object')
    layout.mechanism_instances = {}
    layout.mechanism_entries = {}
    for part in layout.parts:
        counts = {}
        for entry in [*part.modules, *part.elements]:
            kind, sector, position, *rotation = entry
            if not kind.startswith('P_Modul_'):
                continue
            index = counts.get(kind, 0)
            counts[kind] = index + 1
            identifier = f'{part_id(part)}/mechanism/{kind}/{index}'
            value = overrides.get(identifier, {})
            if not isinstance(value, dict) or set(value) - {'offset', 'yaw', 'sector', 'physics'}:
                raise ValueError(f'{identifier}: accepts offset, yaw, sector and physics only')
            offset = vector(value.get('offset', [0, 0, 0]))
            yaw = number(value.get('yaw', 0), 'mechanism yaw')
            active = value.get('sector', sector)
            if type(active) is not int or not 1 <= active <= len(layout.sectors):
                raise ValueError(f'{identifier}: activation sector must be a valid integer sector')
            from .dynamics import parameters, variant, profiles

            physics = parameters(kind, value.get('physics', {}))
            base_yaw = rotation[0] if rotation else 0
            pose = {
                'position': list(place(offset, position, base_yaw)),
                'yaw': (base_yaw + yaw) % 360,
                'sector': active,
                'base_pose': {'position': list(position), 'yaw': base_yaw, 'sector': sector},
                'settings': {'offset': list(offset), 'yaw': yaw, 'sector': active},
            }
            if physics:
                pose['settings']['physics'] = physics
                pose['parameter_schema'] = profiles()[kind]
                pose['runtime_variant'] = variant(identifier, kind, physics)
            layout.mechanism_instances[identifier] = pose
            layout.mechanism_entries[id(entry)] = pose
            part.mechanism_entries[id(entry)] = pose
    unknown = set(overrides) - layout.mechanism_instances.keys()
    if unknown:
        raise ValueError('Unknown mechanism instances: ' + str(sorted(unknown)))
