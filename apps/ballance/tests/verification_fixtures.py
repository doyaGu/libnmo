"""Small trace fixtures; synthetic execution is not native playability evidence."""

from contextlib import ExitStack, contextmanager
import csv
from pathlib import Path
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

from ballance.core.nmo import Nmo
from ballance.route.build import build
from ballance.verify import harness


def write_ball_trace(folder, rows):
    """Rows contain frame, world position and runtime identity, plus fixed keys."""
    with (folder / 'steer.csv').open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(
            [
                'frame',
                'x',
                'y',
                'z',
                'object_id',
                'object_name',
                'next',
                'up',
                'down',
                'left',
                'right',
            ]
        )
        for frame, position, identity, name in rows:
            writer.writerow([frame, *position, identity, name, 0, 1, 0, 0, 0])


def message(frame, event):
    return f'[{frame}] Send Message#1 sends "{event}" to All_Gameplay#2\n'


@contextmanager
def synthetic_runtime(module, route):
    """Build real metadata; substitute only external native assets and packing.

    The caller supplies execution output. Compilation, log parsing, verdicts,
    report writing and evidence hashes still use the production code.
    """
    with tempfile.TemporaryDirectory() as temporary, ExitStack() as stack:
        root = Path(temporary).resolve()
        source = root / 'source'
        if build(route, source, None, 'Regression fixture') != 0:
            raise AssertionError('synthetic route did not build')
        game = root / 'game'
        entities = game / '3D Entities'
        entities.mkdir(parents=True)
        for name, _ in harness.MAIN_FILES:
            (entities / name).write_bytes(b'synthetic dependency')
        executable = root / 'nmo'
        executable.write_bytes(b'synthetic executable identity')
        level = root / 'level.nmo'
        level.write_bytes(b'synthetic packed level')
        base = root / 'base.cmo'
        base.write_bytes(b'synthetic harness')
        stack.enter_context(patch.object(module.project, 'pack', return_value=level))
        stack.enter_context(
            patch.object(module, 'resource_check', return_value={'status': 'passed'})
        )
        stack.enter_context(patch.object(module, 'template_paths', return_value=[]))
        stack.enter_context(patch.object(module, 'dependency_paths', return_value=[]))
        if hasattr(module, 'base_for'):
            stack.enter_context(patch.object(module, 'base_for', return_value=base))
        yield SimpleNamespace(
            root=root,
            source=source,
            game=game,
            nmo=Nmo(str(executable)),
            out=root / 'results',
            specification=root / 'specification.json',
        )
