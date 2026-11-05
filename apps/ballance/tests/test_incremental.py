"""Dependency propagation and incremental mesh equivalence."""

from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ballance.core import project
from ballance.route.build import build


def read(path):
    return json.loads(path.read_text())


ROUTE = {
    'schema_version': 1,
    'sectors': [
        [{'id': 'first', 'road': 20}],
        [
            {'id': 'entry', 'road': 10},
            {'id': 'joint', 'connector': {'length': 20, 'to': {'width': 5}}},
            {'id': 'exit', 'road': 10},
        ],
        [{'id': 'last', 'road': 20}],
    ],
}


class IncrementalTests(unittest.TestCase):
    def test_edited_prefix_rebuilds_downstream_and_matches_clean_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            out, clean = Path(tmp) / 'incremental', Path(tmp) / 'clean'
            build(ROUTE, out, None, 'Test')
            changed = deepcopy(ROUTE)
            changed['sectors'][1][1]['connector']['length'] = 25
            build(changed, out, None, 'Test', incremental=True)
            info = read(out / 'rebuild.json')
            self.assertEqual(info['reused_sectors'], [1])
            self.assertEqual(info['rebuilt_sectors'], [2, 3])
            build(changed, clean, None, 'Test')
            for name in project.inputs(clean):
                self.assertEqual((out / name).read_bytes(), (clean / name).read_bytes(), name)
            self.assertEqual(project.inspect(out)['status'], 'passed')

    def test_tampering_tool_changes_and_sector_count_invalidate_cache(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'level'
            build(ROUTE, out, None, 'Test')
            (out / 'sector2.obj').write_text('broken')
            build(ROUTE, out, None, 'Test', incremental=True)
            self.assertEqual(read(out / 'rebuild.json')['rebuilt_sectors'], [2])
            self.assertIn('v ', (out / 'sector2.obj').read_text())
            tools = project.tools_identity()
            with patch(
                'ballance.core.project.tools_identity', return_value=tools | {'new': 'version'}
            ):
                build(ROUTE, out, None, 'Test', incremental=True)
            self.assertEqual(read(out / 'rebuild.json')['rebuilt_sectors'], [1, 2, 3])
            shortened = deepcopy(ROUTE)
            shortened['sectors'].pop()
            build(shortened, out, None, 'Test', incremental=True)
            self.assertEqual(read(out / 'rebuild.json')['rebuilt_sectors'], [1, 2])
            self.assertFalse((out / 'sector3.obj').exists())

    def test_corrupt_cache_is_never_used(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'level'
            build(ROUTE, out, None, 'Test')
            cache = read(out / 'sector_cache.json')
            cache['1']['files']['sector1.obj'] = project.digest(out / 'sector2.obj')
            (out / 'sector1.obj').write_bytes((out / 'sector2.obj').read_bytes())
            project.write_json(out / 'sector_cache.json', cache)
            build(ROUTE, out, None, 'Test', incremental=True)
            self.assertEqual(read(out / 'rebuild.json')['rebuilt_sectors'], [1, 2, 3])
