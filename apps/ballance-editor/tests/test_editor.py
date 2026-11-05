"""Editing transactions, dependency propagation and incremental mesh equivalence."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from support import TOOLCHAIN_ROOT
from ballance.core import project
from ballance.core.nmo import Nmo
from ballance_editor.session import Session, read
from ballance.route.build import build

ROUTE = {'schema_version': 1, 'sectors': [
    [{'id': 'first', 'road': 20}],
    [{'id': 'entry', 'road': 10}, {'id': 'joint', 'connector': {'length': 20, 'to': {'width': 5}}},
     {'id': 'exit', 'road': 10}],
    [{'id': 'last', 'road': 20}]]}


class EditorTests(unittest.TestCase):
    def make(self, tmp):
        source = Path(tmp)/'route.json'
        project.write_json(source, ROUTE)
        session = Session(source, Path(tmp)/'editor', None, Nmo('nmo'))
        session.prepare()
        return session

    def test_preview_apply_comparison_and_explicit_save(self):
        with tempfile.TemporaryDirectory() as tmp:
            session = self.make(tmp)
            revision = session.revision()
            piece = deepcopy(ROUTE['sectors'][1][1])
            piece['connector']['length'] = 25
            route, preview = session.preview('route/id/joint', piece, revision)
            self.assertEqual(session.route, ROUTE)
            self.assertEqual(read(session.source), ROUTE)
            self.assertIn('route/id/last', preview['impact']['changed_entities'])
            state = session.apply('route/id/joint', piece, revision)
            self.assertTrue(state['before_available'])
            self.assertEqual(state['rebuild']['reused_sectors'], [1])
            self.assertEqual(read(session.source), ROUTE)
            self.assertEqual(read(session.before/'route.json'), ROUTE)
            self.assertEqual(state['checks']['validation.json']['freshness']['status'], 'current')
            self.assertNotIn('full-validation.json', state['checks'])
            self.assertNotEqual(session.scene(True)['model'], session.scene()['model'])
            with self.assertRaisesRegex(ValueError, 'Draft changed'):
                session.apply('route/id/joint', piece, revision)
            session.save(session.revision())
            self.assertEqual(read(session.source), route)
            self.assertTrue(session.state()['saved'])

    def test_invalid_edit_and_failed_build_preserve_draft_and_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            session = self.make(tmp)
            old = (session.current/'sector2.obj').read_bytes()
            piece = deepcopy(ROUTE['sectors'][1][1])
            piece['connector']['to']['width'] = 7
            with self.assertRaises(ValueError):
                session.apply('route/id/joint', piece, session.revision())
            self.assertEqual(session.route, ROUTE)
            piece['connector']['to']['width'] = 5
            piece['connector']['length'] = 25
            with patch('ballance.route.build.write_sector', side_effect=OSError('disk full')):
                with self.assertRaisesRegex(OSError, 'disk full'):
                    session.apply('route/id/joint', piece, session.revision())
            self.assertEqual((session.current/'sector2.obj').read_bytes(), old)
            self.assertEqual(session.route, ROUTE)
            self.assertEqual(read(session.source), ROUTE)
            self.assertEqual(project.inspect(session.current)['status'], 'failed')
            self.assertEqual(read(session.current/'validation.json')['status'], 'superseded')

    def test_external_source_changes_and_identity_replacement_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            session = self.make(tmp)
            piece = deepcopy(ROUTE['sectors'][1][1]);piece['id'] = 'new'
            with self.assertRaisesRegex(ValueError, 'ID'):
                session.preview('route/id/joint', piece, session.revision())
            session.source.write_text('{"sectors": []}')
            with self.assertRaisesRegex(ValueError, 'externally'):
                session.save(session.revision())
            self.assertEqual(session.source.read_text(), '{"sectors": []}')



if __name__ == "__main__": unittest.main()
