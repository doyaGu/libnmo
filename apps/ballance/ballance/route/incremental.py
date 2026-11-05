"""Reuse verified sector meshes; always rebuild global placement and contracts.

The dependency is deliberately conservative: all preceding route sectors,
geometry settings, the total sector count, tools and extracted assets. Scenery,
mechanism interactions and death-zone policies are rebuilt globally; they do not
change sector meshes.
Later sectors may depend on an earlier exit even if their own JSON is unchanged.
"""

import json
import shutil
from pathlib import Path

from ..core import project


def keys(route: dict, elements: Path | None) -> list[str]:
    assets = None
    if elements is not None:
        index = elements / 'resource_index.json'
        assets = project.digest(index) if index.is_file() else 'unrecorded'
    from .templates import expand

    expanded = expand(route)
    common = {
        k: v
        for k, v in route.items()
        if k not in ('sectors', 'definitions', 'overrides', 'scenery', 'mechanisms', 'death_zones')
    }
    common.update(tools=project.tools_identity(), assets=assets, sector_count=len(route['sectors']))
    return [
        project._identity({'settings': common, 'prefix': expanded[:i]})
        for i in range(1, len(route['sectors']) + 1)
    ]


def write(layout, route, out, elements, previous, writer):
    fingerprints = keys(route, elements)
    cache = {}
    reason = 'No matching, intact sector cache.'
    if previous is not None:
        try:
            record = json.loads((previous / 'build_info.json').read_text())
            if record['files'].get('sector_cache.json') == project.digest(
                previous / 'sector_cache.json'
            ):
                cache = json.loads((previous / 'sector_cache.json').read_text())
        except (OSError, ValueError, KeyError):
            pass
    records, reused, rebuilt = [], [], []
    for index, (sector, key) in enumerate(zip(layout.sectors, fingerprints), 1):
        old = cache.get(str(index), {})
        names = [f'{stem}{index}.obj' for stem in ('sector', 'rails', 'ribs', 'railings')]
        files = old.get('files', {})
        intact = (
            old.get('key') == key
            and f'sector{index}.obj' in files
            and set(files) <= set(names)
            and all(
                (previous / name).is_file() and project.digest(previous / name) == digest
                for name, digest in files.items()
            )
        )
        if intact:
            for name in files:
                shutil.copyfile(previous / name, out / name)
            reused.append(index)
        else:
            writer(sector, index, out)
            rebuilt.append(index)
        records.append(
            (
                str(index),
                {
                    'key': key,
                    'files': {
                        name: project.digest(out / name) for name in names if (out / name).is_file()
                    },
                },
            )
        )
    project.write_json(out / 'sector_cache.json', dict(records))
    project.write_json(
        out / 'rebuild.json',
        {
            'reused_sectors': reused,
            'rebuilt_sectors': rebuilt,
            'reason': 'Matching route prefix, settings, generator and verified mesh hashes.'
            if reused
            else reason,
            'global_recomputed': [
                'layout',
                'connections',
                'resources',
                'scenery',
                'mechanisms',
                'death_zones',
                'gameplay_contracts',
                'manifest',
            ],
            'play_evidence': 'Not inherited; changed generations require a new run.',
        },
    )
