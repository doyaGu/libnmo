"""Authoritative descriptions of composite native level resources.

Extraction, fallback geometry, route adapters and spatial reservations read
these same descriptions. Installation-specific hashes live with exports.
"""

import hashlib
import json
from pathlib import Path

CATALOG = Path(__file__).resolve().parents[2] / 'data/resource_catalog.json'
INDEX = 'resource_index.json'


def catalog():
    data = json.loads(CATALOG.read_text(encoding='utf-8'))
    if data.get('schema_version') != 1 or not isinstance(data.get('resources'), dict):
        raise ValueError('unsupported native resource catalog')
    return data['resources']


def resource(name):
    return catalog()[name]


def identity(description):
    return hashlib.sha256(
        json.dumps(description, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()
    ).hexdigest()


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def files(description):
    runtime = description.get('runtime', {})
    return sorted(
        {p['extract']['file'] for p in description['parts']}
        | ({runtime['template'], *runtime.get('dependencies', [])} if runtime else set())
    )


def mechanisms():
    """Native marker kind -> (author-facing name, shared description)."""
    return {
        spec['placement'].get('module', spec['placement'].get('element')): (name, spec)
        for name, spec in catalog().items()
        if spec.get('category') == 'mechanism'
    }


def elements():
    """All native element markers, independent of motion-model coverage."""
    return {
        spec['placement'].get('module')
        or spec['placement'].get('element')
        or spec['placement'].get('marker'): (name, spec)
        for name, spec in catalog().items()
        if spec.get('category') in ('mechanism', 'element')
    }


def assemblies():
    return {
        spec['placement']['module_floor']: (name, spec)
        for name, spec in catalog().items()
        if 'module_floor' in spec.get('placement', {})
    }


def scenery_resource(role):
    return next(
        (name, spec)
        for name, spec in catalog().items()
        if spec.get('category') == 'decoration' and spec['placement']['role'] == role
    )


def check_export(folder, name, record):
    description = resource(name)
    if record.get('description_sha256') != identity(description):
        raise ValueError(f'{name}: resource description changed; refresh the extracted resources')
    expected = {
        part['name'] + suffix for part in description['parts'] for suffix in ('.obj', '.mtl')
    }
    if set(record.get('assets', {})) != expected or set(record.get('sources', {})) != set(
        files(description)
    ):
        raise ValueError(f'{name}: incomplete resource extraction record; refresh resources')
    for filename, sha in record['assets'].items():
        path = Path(folder) / filename
        if not path.is_file() or digest(path) != sha:
            raise ValueError(
                f'{name}: extracted asset changed or missing: {filename}; refresh resources'
            )
    return record
