"""Bind reports, images and traces to the generation that produced them.

Capture before doing the work, then attach to each output. A 'current'
record describes provenance only; it does not mean its checks passed.
"""

from __future__ import annotations

import json
from pathlib import Path

from . import project


def capture(source: Path, kind: str, parameters: dict, dependencies=()) -> dict:
    source = source.resolve()
    if source.is_dir():
        build = project.inspect(source)
        identity = {'build_id': build.get('build_id'), 'status': build['status']}
    else:
        identity = {'sha256': project.digest(source)}
    return {
        'schema_version': 1,
        'source': str(source),
        'identity': identity,
        'kind': kind,
        'parameters': parameters,
        'tools': project.tools_identity(),
        'dependencies': {str(Path(p).resolve()): project.digest(Path(p)) for p in dependencies},
    }


def attach(artifact: Path, record: dict) -> None:
    project.write_json(
        Path(str(artifact) + '.evidence.json'),
        record | {'artifact_sha256': project.digest(artifact)},
    )


def inspect(artifact: Path) -> dict:
    """Check a sidecar without trusting a previously reported success."""
    sidecar = Path(str(artifact) + '.evidence.json')
    if not sidecar.is_file():
        return {'status': 'unavailable', 'reasons': ['No evidence record.']}
    reasons = []
    try:
        record = json.loads(sidecar.read_text(encoding='utf-8'))
        if record['schema_version'] != 1:
            raise ValueError('unsupported evidence schema')
        if project.digest(artifact) != record['artifact_sha256']:
            reasons.append('Artifact content changed.')
        current = capture(
            Path(record['source']), record['kind'], record['parameters'], record['dependencies']
        )
        for key in ('identity', 'tools', 'dependencies'):
            if record[key] != current[key]:
                reasons.append(f'{key} changed.')
        identity = current['identity']
        if 'status' in identity and (
            identity['status'] != 'passed' or not identity.get('build_id')
        ):
            reasons.append('Source has no intact versioned build.')
    except (OSError, ValueError, KeyError, TypeError) as error:
        reasons.append(str(error))
    return {'status': 'stale' if reasons else 'current', 'reasons': reasons}
