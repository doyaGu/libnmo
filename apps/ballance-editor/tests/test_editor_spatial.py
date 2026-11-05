"""Editor authoring against native placement, motion and global constraints."""

from copy import deepcopy
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from ballance.core import project, evidence
from ballance.core.nmo import Nmo
from ballance_editor.model import model, edited_route
from ballance_editor.session import Session, read
from ballance.route.build import build
from ballance.route.templates import expand

BASE = {
    'schema_version': 1,
    'sectors': [
        [{'id': 'first', 'road': 20}],
        [
            {'id': 'entry', 'road': 10},
            {'id': 'sacks', 'resource': 'Sack', 'count': 1},
            {'id': 'exit', 'road': 20},
        ],
    ],
}


class SpatialEditorTests(unittest.TestCase):
    def make(self, tmp, route=BASE, elements=None):
        source = Path(tmp) / 'source.json'
        project.write_json(source, route)
        session = Session(
            source, Path(tmp) / 'editor', elements, Nmo(os.environ.get('NMO_TEST_CLI', 'nmo'))
        )
        session.prepare()
        return session

    def test_owner_edits_recompute_count_states_and_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.make(tmp)
            e = next(e for e in s.state()['model']['entities'] if e['id'] == 'route/id/sacks')
            p = deepcopy(e['piece'])
            p['count'] = 2
            _, preview = s.preview(e['id'], p, s.revision())
            self.assertEqual(len(preview['model']['mechanisms']['instances']), 2)
            self.assertEqual(preview['impact']['rebuild_candidates'], [2])
            self.assertTrue(preview['spatial_checks'])
            self.assertTrue(
                any(
                    v['rule'] == 'mechanism free-joint reach'
                    for v in preview['model']['volumes']
                )
            )
            state = s.apply(e['id'], p, s.revision())
            self.assertEqual(state['rebuild']['reused_sectors'], [1])
            self.assertEqual(read(s.source), BASE)
            self.assertEqual(len(state['model']['mechanisms']['instances']), 2)
            bad = p | {'initial': 'moving'}
            with self.assertRaises(ValueError):
                s.preview(e['id'], bad, s.revision())
            bad = p | {'count': 5}
            with self.assertRaisesRegex(ValueError, 'count'):
                s.preview(e['id'], bad, s.revision())

    def test_template_mechanisms_keep_repeat_overrides_isolated(self):
        route = deepcopy(BASE)
        route['definitions'] = {
            'pack': {
                'pieces': [
                    {'id': 'sack', 'resource': 'Sack', 'count': 1},
                    {'id': 'dock', 'road': 10},
                ]
            }
        }
        route['sectors'][1][1] = {'id': 'pack', 'use': 'pack', 'repeat': 2}
        m = model(route)
        e = next(e for e in m['entities'] if e['category'] == 'resource' and e['editable'])
        p = deepcopy(e['piece'])
        p['count'] = 2
        changed, _, _ = edited_route(route, e, p, 'instance')
        counts = [p['count'] for sec in expand(changed) for p in sec if 'module' in p]
        self.assertEqual(counts, [2, 1])
        self.assertEqual(route['definitions'], changed['definitions'])

    def test_death_policy_rebuilds_global_outputs_without_remeshing_and_matches_clean(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.make(tmp)
            policy = {
                'clearance': 20,
                'keepouts': [{'box': [0, 5, -40, -20, 0, 5], 'reason': 'Recovery volume'}],
            }
            _, preview = s.preview('editor/settings/death_zones', policy, s.revision())
            self.assertEqual(preview['impact']['rebuild_candidates'], [])
            state = s.apply('editor/settings/death_zones', policy, s.revision())
            self.assertEqual(state['rebuild']['reused_sectors'], [1, 2])
            self.assertEqual(read(s.current / 'death_zones.json')['policy'], policy)
            clean = Path(tmp) / 'clean'
            build(s.route, clean, None, 'Rail editor')
            for name in project.inputs(clean):
                self.assertEqual(
                    (clean / name).read_bytes(), (s.current / name).read_bytes(), name
                )
            self.assertEqual(
                evidence.inspect(s.current / 'validation.json')['status'], 'current'
            )

    def test_invalid_references_and_boxes_preserve_current_generation_and_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.make(tmp)
            revision = s.revision()
            previous = {
                n: (s.current / n).read_bytes() for n in ('build_info.json', 'validation.json')
            }
            cases = [
                (
                    'editor/settings/death_zones',
                    {'keepouts': [{'box': [5, 0, 0, 1, 0, 1], 'reason': 'bad'}]},
                ),
                (
                    'editor/settings/mechanisms',
                    {
                        'interactions': [
                            {
                                'mechanism': 'missing',
                                'target': 'route/id/first',
                                'contacts': ['rail'],
                                'phases': ['ready'],
                                'reason': 'test',
                            }
                        ]
                    },
                ),
                (
                    'editor/settings/scenery',
                    {
                        'instances': [
                            {
                                'id': 'lamp',
                                'resource': 'LanternTower',
                                'parent': 'missing',
                                'at': [0, 0, 0],
                            }
                        ]
                    },
                ),
            ]
            for identifier, value in cases:
                with self.assertRaises(ValueError):
                    s.preview(identifier, value, revision)
                self.assertEqual(s.revision(), revision)
                self.assertEqual(read(s.root / 'draft.json'), BASE)
                for name, content in previous.items():
                    self.assertEqual((s.current / name).read_bytes(), content)
            self.assertFalse(list(s.root.glob('.preview-*')))

    def test_failed_spatial_report_cannot_publish_a_candidate(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.make(tmp)
            before = (s.current / 'build_info.json').read_bytes()
            failed = {'checks': {'scenery': {'status': 'failed', 'findings': ['collision']}}}
            with patch('ballance_editor.session.report.verify', return_value=failed):
                with self.assertRaisesRegex(ValueError, 'Spatial preview failed'):
                    s.apply('editor/settings/death_zones', {'clearance': 20}, s.revision())
            self.assertEqual(s.route, BASE)
            self.assertEqual((s.current / 'build_info.json').read_bytes(), before)


def e_id(name):
    return 'scenery/id/' + name


if __name__ == '__main__':
    unittest.main()
