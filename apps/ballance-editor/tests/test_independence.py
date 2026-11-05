"""The application can relocate without becoming a toolchain dependency."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from support import APP_ROOT, TOOLCHAIN_ROOT


class IndependenceTests(unittest.TestCase):
    def test_relocated_launcher_uses_explicit_external_toolchain(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp) / 'standalone'
            shutil.copytree(
                APP_ROOT,
                app,
                ignore=shutil.ignore_patterns('__pycache__', 'node_modules', '.ruff_cache'),
            )
            result = subprocess.run(
                [sys.executable, str(app / 'run.py'), '--toolchain', str(TOOLCHAIN_ROOT), '--help'],
                cwd=tmp,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('--elements', result.stdout)
            self.assertIn('--toolchain', result.stdout)
            self.assertTrue((app / 'ballance_editor/editor.html').is_file())

    def test_toolchain_build_and_evidence_survive_application_changes_and_removal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            toolkit = root / 'toolkit'
            shutil.copytree(
                TOOLCHAIN_ROOT,
                toolkit,
                ignore=shutil.ignore_patterns('__pycache__', 'node_modules', '.ruff_cache'),
            )
            app = root / 'ballance-editor'
            shutil.copytree(
                APP_ROOT,
                app,
                ignore=shutil.ignore_patterns('__pycache__', 'node_modules', '.ruff_cache'),
            )
            script = r"""
import importlib.abc
from pathlib import Path
import shutil
import sys

class NoEditor(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if (fullname == 'ballance_editor' or fullname.startswith('ballance_editor.')
                or fullname.startswith('ballance.editor')):
            raise AssertionError('Toolchain imported editor: ' + fullname)
sys.meta_path.insert(0, NoEditor())
from ballance.core import project, evidence
from ballance.route.build import build

route = {'schema_version': 1, 'sectors': [[{'road': 20}]]}
out = Path('built')
build(route, out, None, 'Independent')
assert project.inspect(out)['status'] == 'passed'
identity = project.tools_identity()
artifact = Path('report.json')
artifact.write_text('{}')
evidence.attach(artifact, evidence.capture(out, 'verification', {}))
before = {p.name: project.digest(p) for p in out.glob('*.obj')}
assert before
app = Path('ballance-editor')
for extension in ('py', 'js', 'html', 'css'):
    path = next((app / 'ballance_editor').glob('*.' + extension))
    path.write_text('changed application')
assert project.tools_identity() == identity
assert project.inspect(out)['status'] == 'passed'
assert evidence.inspect(artifact)['status'] == 'current'
shutil.rmtree(app)
assert project.tools_identity() == identity
assert project.inspect(out)['status'] == 'passed'
assert evidence.inspect(artifact)['status'] == 'current'
build(route, Path('rebuilt'), None, 'Independent')
assert {p.name: project.digest(p) for p in Path('rebuilt').glob('*.obj')} == before
"""
            result = subprocess.run(
                [sys.executable, '-c', script],
                cwd=root,
                env=os.environ | {'PYTHONPATH': str(toolkit)},
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
