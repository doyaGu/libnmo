"""Source identity survives author edits and template transformations."""

import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.route.layout import Layout
from ballance.route.templates import expand, mirror


class IdentityTests(unittest.TestCase):
    def test_insert_and_edit_preserve_named_entities_and_update_source_locations(self):
        route = {
            'schema_version': 1,
            'sectors': [[{'id': 'entry', 'road': 10}, {'id': 'exit', 'road': 20}]],
        }
        before = Layout(route).source_map()['entities']
        route['sectors'][0].insert(1, {'id': 'landing', 'road': 5})
        route['sectors'][0][2]['road'] = 30
        after = Layout(route).source_map()['entities']
        old = next(p for p in before if p['id'] == 'route/id/exit')
        new = next(p for p in after if p['id'] == old['id'])
        self.assertEqual(old['source']['pointer'], '/sectors/0/1')
        self.assertEqual(new['source']['pointer'], '/sectors/0/2')
        self.assertTrue(new['source']['stable'])
        self.assertEqual(new['after'], 'route/id/landing')
        self.assertEqual(new['kind'], 'road')

    def test_nested_repeated_mirrored_templates_keep_distinct_instance_origins(self):
        route = {
            'schema_version': 1,
            'definitions': {
                'outer': {'pieces': [{'id': 'child', 'use': 'inner', 'repeat': 2}]},
                'inner': {
                    'parameters': {'length': 10},
                    'pieces': [
                        {'id': 'bend', 'turn': 'left'},
                        {'id': 'run', 'road': '${length}'},
                    ],
                },
            },
            'sectors': [[{'id': 'loop', 'use': 'outer', 'repeat': 2, 'mirror': True}]],
        }
        original = copy.deepcopy(route)
        pieces = expand(route)[0]
        self.assertEqual(len({p.origin.entity_id for p in pieces}), 8)
        self.assertTrue(all(p.origin.stable for p in pieces))
        self.assertEqual(pieces[0]['turn'], 'right')
        self.assertEqual(pieces[0].origin.pointer, '/definitions/inner/pieces/0')
        self.assertEqual(
            [i['template'] for i in pieces[0].origin.instances], ['outer', 'inner']
        )
        self.assertEqual(mirror(mirror(pieces[0])).origin, pieces[0].origin)
        self.assertEqual(route, original)
        # Inserting a separate instance must not rename either nested repetition.
        route['sectors'][0].insert(0, {'id': 'another', 'use': 'outer'})
        self.assertEqual(
            [p.origin for p in pieces][0].entity_id, expand(route)[0][4].origin.entity_id
        )

    def test_branch_entities_are_scoped_and_reported_at_the_actual_source(self):
        route = {
            'schema_version': 1,
            'sectors': [
                [
                    {
                        'id': 'hub',
                        'area': [20, 20],
                        'branches': [
                            {
                                'id': 'spur',
                                'side': 'left',
                                'visit': True,
                                'route': [{'id': 'run', 'road': 10}],
                            }
                        ],
                    },
                    {'id': 'run', 'road': 10},
                ]
            ],
        }
        entities = Layout(route).source_map()['entities']
        branch = next(p for p in entities if '/branches/' in p['id'])
        self.assertTrue(branch['source']['stable'])
        self.assertEqual(branch['after'], 'route/id/hub')
        self.assertEqual(branch['source']['pointer'], '/sectors/0/0/branches/0/route/0')

    def test_legacy_ids_are_explicitly_positional_and_invalid_ids_fail(self):
        legacy = expand({'sectors': [[{'road': 10}]]})[0][0]
        self.assertFalse(legacy.origin.stable)
        for route, message in [
            ({'sectors': [[{'id': 'a', 'road': 10}]]}, 'schema_version'),
            ({'schema_version': 2, 'sectors': []}, 'schema_version'),
            ({'schema_version': 1, 'sectors': [[{'id': '../a', 'road': 10}]]}, '/id'),
            (
                {
                    'schema_version': 1,
                    'sectors': [[{'id': 'a', 'road': 10}], [{'id': 'a', 'road': 20}]],
                },
                'duplicate',
            ),
        ]:
            with self.subTest(route=route), self.assertRaisesRegex(ValueError, message):
                expand(route)

    def test_connection_failure_contains_entity_and_source_pointer(self):
        with self.assertRaisesRegex(ValueError, r'route/id/bad at /sectors/0/1'):
            Layout(
                {
                    'schema_version': 1,
                    'sectors': [
                        [
                            {'id': 'entry', 'road': 10},
                            {
                                'id': 'bad',
                                'floor_path': {'width': 9, 'segments': [{'line': [10, 0, 0]}]},
                            },
                        ]
                    ],
                }
            )


if __name__ == '__main__':
    unittest.main()
