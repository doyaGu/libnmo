"""Template editing isolation, scope and geometry cache acceptance tests."""

from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

from ballance.core import project
from ballance_editor.model import model, edited_route, impact
from ballance_editor.session import read
from ballance.route.build import build
from ballance.route.editing import leaves
from ballance.route.incremental import keys
from ballance.route.templates import expand


def fixture():
    return {
        'schema_version': 1,
        'definitions': {
            'strip': {
                'parameters': {'length': 20},
                'pieces': [{'id': 'road', 'road': '${length}'}],
            },
            'pair': {
                'parameters': {},
                'pieces': [{'id': 'inner', 'use': 'strip', 'repeat': 2}],
            },
        },
        'sectors': [
            [{'id': 'start', 'road': 10}],
            [{'id': 'pair', 'use': 'pair', 'repeat': 2, 'mirror': True}],
            [
                {'id': 'other', 'use': 'strip', 'with': {'length': 20}},
                {'id': 'end', 'road': 20},
            ],
        ],
    }


def select(route, suffix=None):
    entities = [e for e in model(route)['entities'] if e['editable']]
    return next(e for e in entities if e['id'].endswith(suffix)) if suffix else entities[1]


def edit(route, entity, piece, scope=None, reset=False):
    candidate, scope, pointer = edited_route(route, entity, piece, scope, reset)
    return candidate, impact(model(route), model(candidate), entity['id'], scope, pointer)


class TemplateEditorTests(unittest.TestCase):
    def test_nested_mirrored_repeats_are_isolated_persistent_and_resettable(self):
        route = fixture()
        entity = select(route)
        piece = deepcopy(entity['piece'])
        piece['road'] = 25
        changed, info = edit(route, entity, piece)
        self.assertEqual(changed['definitions'], route['definitions'])
        self.assertEqual(changed['sectors'], route['sectors'])
        expanded = list(leaves(expand(json.loads(json.dumps(changed)))))
        self.assertEqual(sum(p.get('road') == 25 for p in expanded), 1)
        self.assertTrue(
            next(p for p in expanded if p.get('road') == 25).origin.override_pointer
        )
        self.assertEqual(info['rebuild_candidates'], [2, 3])
        reset, _ = edit(changed, select(changed), None, 'instance', True)
        self.assertEqual(reset, route)
        # Updating an existing override compares against the template, not the old patch.
        piece['road'] = 30
        again, _ = edit(changed, select(changed), piece)
        self.assertEqual(again['overrides'][entity['id']]['set'], {'road': 30})

    def test_call_scope_repeat_additions_and_shared_defaults_respect_explicit_values(self):
        route = fixture()
        entity = select(route)
        call = deepcopy(entity['edit_targets']['call']['piece'])
        call['repeat'] = 3
        changed, info = edit(route, entity, call, 'call')
        self.assertEqual(len(info['added_entities']), 2)
        self.assertFalse(info['removed_entities'])
        self.assertEqual(changed['sectors'][2], route['sectors'][2])
        changed, info = edit(route, entity, {'parameters': {'length': 25}}, 'template')
        resolved = list(leaves(expand(changed)))
        self.assertEqual(sum(p.get('road') == 25 for p in resolved), 4)
        self.assertEqual(next(p for p in resolved if 'other' in p.origin.entity_id)['road'], 20)
        # Downstream positions change, though explicitly supplied local geometry does not.
        self.assertEqual(keys(route, None)[0], keys(changed, None)[0])
        self.assertNotEqual(keys(route, None)[1], keys(changed, None)[1])

    def test_orphan_and_identity_changes_cannot_silently_drop_edits(self):
        route = fixture()
        entity = select(route, 'pair/repeat/1/id/inner/repeat/1/id/road')
        piece = deepcopy(entity['piece'])
        piece['road'] = 25
        changed, _ = edit(route, entity, piece)
        call = deepcopy(entity['edit_targets']['call']['piece'])
        call['repeat'] = 1
        with self.assertRaisesRegex(ValueError, 'orphan'):
            edit(changed, select(changed, entity['id']), call, 'call')
        for patch in (
            {'set': {'id': 'wrong'}},
            {'remove': ['road']},
            {'set': {'branches': []}},
        ):
            bad = deepcopy(route)
            bad['overrides'] = {entity['id']: patch}
            with self.assertRaises(ValueError):
                expand(bad)
        piece['id'] = 'wrong'
        with self.assertRaisesRegex(ValueError, 'ID'):
            edit(route, entity, piece)

    def test_parent_and_branch_overrides_survive_in_either_order(self):
        platform = {
            'id': 'platform',
            'floor_polygon': {
                'outline': [[0, -15], [40, -15], [40, 15], [0, 15]],
                'holes': [[[15, 4], [25, 4], [25, 10], [15, 10]]],
                'entry': {'edge': 3},
                'exit': {'edge': 1},
                'ports': {'paper': {'edge': 0, 'path': [[20, 0]]}},
            },
            'branches': [
                {
                    'id': 'spur',
                    'port': 'paper',
                    'visit': True,
                    'route': [
                        {'id': 'road', 'road': 10},
                        {'id': 'trafo', 'trafo': 'paper'},
                        {'id': 'end', 'road': 5},
                    ],
                }
            ],
        }
        route = {
            'schema_version': 1,
            'definitions': {'hub': {'pieces': [platform]}},
            'sectors': [
                [
                    {'id': 'entry', 'road': 10},
                    {'id': 'call', 'use': 'hub'},
                    {'id': 'end', 'road': 10},
                ]
            ],
        }
        parent = next(p for p in leaves(expand(route)) if 'floor_polygon' in p)
        child = next(p for p in leaves(expand(route)) if p.get('id') == 'road')
        spec = deepcopy(parent['floor_polygon'])
        spec['border'] = 1
        patches = [
            (parent.origin.entity_id, {'set': {'floor_polygon': spec}}),
            (
                child.origin.entity_id,
                {'set': {'road': 15, 'elements': [{'resource': 'ExtraPoint', 'at': [5, 0]}]}},
            ),
        ]
        for ordered in (patches, patches[::-1]):
            changed = route | {'overrides': dict(ordered)}
            values = list(leaves(expand(changed)))
            self.assertEqual(
                next(p for p in values if 'floor_polygon' in p)['floor_polygon']['border'], 1
            )
            leaf = next(p for p in values if p.origin.entity_id == child.origin.entity_id)
            self.assertEqual(leaf['road'], 15)
            self.assertEqual(leaf['elements'][0]['kind'], 'P_Extra_Point')

    def test_mirror_override_uses_effective_local_coordinates(self):
        route = {
            'schema_version': 1,
            'definitions': {
                'bend': {
                    'pieces': [
                        {
                            'id': 'rail',
                            'rail_path': {
                                'segments': [{'bezier': [[5, 0, 0], [15, 4, 0], [20, 4, 0]]}]
                            },
                        }
                    ]
                }
            },
            'sectors': [
                [
                    {'id': 'entry', 'road': 10},
                    {'id': 'copy', 'use': 'bend', 'mirror': True},
                    {'id': 'end', 'road': 10},
                ]
            ],
        }
        e = select(route)
        p = deepcopy(e['piece'])
        self.assertEqual(p['rail_path']['segments'][0]['bezier'][-1][1], -4)
        p['rail_path']['segments'][0]['bezier'][0][0] = 6
        changed, _ = edit(route, e, p)
        self.assertEqual(select(changed)['piece'], p)

    def test_template_incremental_build_matches_clean_and_unused_defaults_reuse(self):
        route = fixture()
        e = select(route)
        p = deepcopy(e['piece'])
        p['road'] = 25
        changed, _ = edit(route, e, p)
        with tempfile.TemporaryDirectory() as tmp:
            out, clean = Path(tmp) / 'inc', Path(tmp) / 'clean'
            build(route, out, None, 'Test')
            build(changed, out, None, 'Test', incremental=True)
            self.assertEqual(read(out / 'rebuild.json')['reused_sectors'], [1])
            build(changed, clean, None, 'Test')
            for name in project.inputs(clean):
                self.assertEqual((out / name).read_bytes(), (clean / name).read_bytes(), name)
            changed['definitions']['unused'] = {'parameters': {'length': 2}, 'pieces': []}
            build(changed, out, None, 'Test', incremental=True)
            self.assertEqual(read(out / 'rebuild.json')['reused_sectors'], [1, 2, 3])


if __name__ == '__main__':
    unittest.main()
