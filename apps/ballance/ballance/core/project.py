"""A built project's inputs and their content identity.

The manifest is the inventory: unused files in an output directory do not
belong to the current build. Repacking always uses its current meshes.
"""

from __future__ import annotations

import hashlib
import json
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
import shutil
import tempfile
import uuid

from .nmo import Nmo

BUILD_RECORD = 'build_info.json'
ROUTE_SNAPSHOT = 'route.json'
SIDE_FILES = (
    ROUTE_SNAPSHOT,
    'route_points.json',
    'route_sectors.json',
    'death_zones.json',
    'scenery_placement.json',
    'source_map.json',
    'connections.json',
    'spatial_constraints.json',
    'gameplay_contracts.json',
    'resources.json',
    'sector_cache.json',
    'native_dynamics.json',
)


def _identity(value: dict) -> str:
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('utf-8')
    ).hexdigest()


def tools_identity() -> dict:
    """Conservative generator/controller identity, including dirty source edits."""
    root = Path(__file__).resolve().parents[2]
    files = [
        *root.joinpath('ballance').rglob('*.py'),
        *root.joinpath('scripts').glob('*.py'),
        root / 'data/level_materials.json',
        root / 'data/motion_catalog.json',
        root / 'data/resource_catalog.json',
        root / 'data/mechanism_parameters.json',
    ]
    return {str(path.relative_to(root)): digest(path) for path in sorted(files)}


def write_json(path: Path, value: dict) -> None:
    # Readers see the complete old or new JSON, never a truncated report.
    with tempfile.NamedTemporaryFile(
        mode='w', encoding='utf-8', dir=path.parent, prefix=f'.{path.name}-', delete=False
    ) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(json.dumps(value, indent=2, allow_nan=False) + '\n')
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def _attempt(out: Path, route: dict, status: str, reason: str) -> None:
    out.mkdir(parents=True, exist_ok=True)
    write_json(
        out / 'build_attempt.json',
        {
            'schema_version': 1,
            'status': status,
            'reason': reason,
            'request_sha256': _identity(route),
            'time': datetime.now(timezone.utc).isoformat(),
        },
    )
    for name in ('validation.json', 'preview-validation.json'):
        path = out / name
        if path.is_file():
            try:
                old = json.loads(path.read_text(encoding='utf-8'))
            except (ValueError, UnicodeError):
                continue
            if isinstance(old, dict):
                old.setdefault('previous_status', old.get('status'))
                old.update(status='superseded', reason=reason)
                write_json(path, old)


@contextmanager
def generation(out: Path, route: dict):
    """Stage all writes; archive the previous directory and publish on success.

    Publication uses directory renames on the same filesystem. There can be a
    brief absent path between renames, never a directory mixing two generations.
    A failed rename is rolled back. History is retained under the new .history/.
    """
    if out.is_symlink():
        raise ValueError('build output must not be a symlink')
    out = out.absolute()
    cwd = Path.cwd().resolve()
    if out.resolve() == cwd or out.resolve() in cwd.parents:
        raise ValueError('build output must be a dedicated level directory')
    out.parent.mkdir(parents=True, exist_ok=True)
    lock = out.parent / f'.{out.name}.build-lock'
    try:
        lock.mkdir()
    except FileExistsError as error:
        raise ValueError(
            f'build lock exists: {lock}; check for an active builder before removing it'
        ) from error
    staged = None
    preserve_staged = False
    try:
        _attempt(out, route, 'building', 'A new build was requested; prior evidence is historical.')
        staged = Path(tempfile.mkdtemp(prefix=f'.{out.name}.build-', dir=out.parent))
        yield staged
        history = staged / '.history'
        previous = history / uuid.uuid4().hex
        moved_history = False
        try:
            if (out / '.history').exists():
                (out / '.history').rename(history)
                moved_history = True
            else:
                history.mkdir()
            out.rename(previous)
            staged.rename(out)
        except BaseException:
            try:
                if previous.exists():
                    previous.rename(out)
                if moved_history:
                    history.rename(out / '.history')
            except BaseException as recovery_error:
                preserve_staged = True
                raise RuntimeError(
                    f'Publication rollback failed; recovery files retained at {staged}'
                ) from recovery_error
            raise
    except BaseException as error:
        _attempt(out, route, 'failed', f'Build failed; previous generation retained: {error}')
        raise
    finally:
        if staged is not None and staged.exists() and not preserve_staged:
            shutil.rmtree(staged)
        lock.rmdir()


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs(folder: Path) -> list[str]:
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
    files = {'manifest.json'}
    for scene in manifest.get('scenes', []):
        for obj in scene.get('objects', []):
            mesh = (obj.get('mesh') or {}).get('obj')
            if mesh:
                files.add(mesh)
    files.update(name for name in SIDE_FILES if (folder / name).is_file())
    from ..route.dynamics import dependencies

    files.update(dependencies(folder))
    return sorted(files)


def record(folder: Path, route: dict, warnings: list[str], native_assets: bool) -> None:
    """Save the source snapshot and hashes after all build outputs are written."""
    (folder / ROUTE_SNAPSHOT).write_text(json.dumps(route, indent=2) + '\n', encoding='utf-8')
    data = {
        'schema_version': 2,
        'native_assets': native_assets,
        'warnings': warnings,
        'tools': tools_identity(),
        'files': {name: digest(folder / name) for name in inputs(folder)},
    }
    data['build_id'] = _identity(data)
    (folder / BUILD_RECORD).write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    # A successful rebuild makes the prior verification report obsolete.
    (folder / 'validation.json').unlink(missing_ok=True)


def inspect(folder: Path) -> dict:
    """Report missing inputs and changes since the builder last completed."""
    names = inputs(folder)
    missing = [name for name in names if not (folder / name).is_file()]
    path = folder / BUILD_RECORD
    if not path.is_file():
        return {
            'status': 'failed' if missing else 'unavailable',
            'missing': missing,
            'changed': [],
            'reason': 'No build record; rebuild to record source and mesh hashes.',
        }
    data = json.loads(path.read_text(encoding='utf-8'))
    if data.get('schema_version') not in (1, 2):
        raise ValueError(f'unsupported build record version in {path}')
    expected = data['files']
    missing = sorted(set(missing) | {name for name in expected if not (folder / name).is_file()})
    changed = sorted(
        {
            name
            for name in expected
            if name not in missing and digest(folder / name) != expected[name]
        }
        | (set(names) - set(expected))
    )
    tool_changes = []
    if data.get('schema_version') == 2:
        actual = tools_identity()
        tool_changes = sorted(
            name
            for name in data['tools'].keys() | actual.keys()
            if data['tools'].get(name) != actual.get(name)
        )
        identity_data = {k: v for k, v in data.items() if k != 'build_id'}
        if data.get('build_id') != _identity(identity_data):
            changed.append(BUILD_RECORD)
    attempt_path = folder / 'build_attempt.json'
    attempt = (
        json.loads(attempt_path.read_text(encoding='utf-8')) if attempt_path.is_file() else None
    )
    return {
        'status': 'failed' if missing or changed or tool_changes or attempt else 'passed',
        'missing': missing,
        'changed': changed,
        'build_id': data.get('build_id'),
        'tool_changes': tool_changes,
        'reason': attempt['reason'] if attempt else '',
        'route_sha256': expected.get(ROUTE_SNAPSHOT),
        'native_assets': data['native_assets'],
        'warnings': data['warnings'],
    }


def pack(folder: Path, nmo: Nmo) -> Path:
    """Patch from current inputs, including meshes edited after the manifest."""
    folder = folder.resolve()
    for name in inputs(folder):
        if not (folder / name).is_file():
            raise ValueError(f'missing project input: {folder / name}')
    level = folder / 'Level_01.NMO'
    nmo.run('patch', 'apply', '--project', str(folder / 'manifest.json'), '-o', str(level))
    return level
