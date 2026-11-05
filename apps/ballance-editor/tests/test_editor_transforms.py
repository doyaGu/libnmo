from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
import support  # noqa: F401 - resolve the external toolchain before application imports
from ballance_editor.model import model
from ballance_editor.transforms import transform, snap, candidates
from ballance_editor.session import Session, read
from ballance.core.nmo import Nmo
from ballance.core import project

ROUTE = {
    'schema_version': 1,
    'sectors': [
        [
            {'id': 'a', 'road': 10},
            {'id': 'b', 'rail_path': {'segments': [{'line': [20, 0, 0]}]}},
            {'id': 'c', 'road': 10},
            {'id': 'd', 'road': 10},
        ]
    ],
}


def by_id(view):
    return {e['id']: e for e in view['entities']}


class TransformTests(unittest.TestCase):
    def test_group_transform_preserves_spacing_and_unselected_objects(self):
        before = model(ROUTE)
        old = by_id(before)
        route, _ = transform(
            ROUTE, before, {'ids': ['route/id/b', 'route/id/c'], 'delta': [10, 5, 20], 'angle': 90}
        )
        after = model(route)
        new = by_id(after)
        self.assertEqual(old['route/id/a']['entry'], new['route/id/a']['entry'])
        self.assertEqual(old['route/id/d']['entry'], new['route/id/d']['entry'])
        self.assertEqual(
            new['route/id/b']['end']['position'], new['route/id/c']['entry']['position']
        )
        self.assertEqual(len(after['connections']['open_connections']), 2)
        option = next(
            x
            for x in candidates(after, ['route/id/b', 'route/id/c'])
            if x['source']['port'] == 'entry' and x['target']['entity'] == 'route/id/a'
        )
        restored, _ = snap(route, after, option)
        final = model(restored)
        self.assertEqual(final['connections']['open_connections'], [])
        for key in ('route/id/a', 'route/id/b', 'route/id/c', 'route/id/d'):
            for a, b in zip(old[key]['end']['position'], by_id(final)[key]['end']['position']):
                self.assertAlmostEqual(a, b)

    def test_snap_rejects_self_and_incompatible_sections(self):
        route = deepcopy(ROUTE)
        route['sectors'][0][1]['rail_path']['gauge'] = 4
        # A nonstandard rail cannot attach to native floor even before placement.
        with self.assertRaises(ValueError):
            model(route)
        view = model(ROUTE)
        with self.assertRaises(ValueError):
            snap(
                ROUTE,
                view,
                {
                    'ids': ['route/id/b'],
                    'source': {'entity': 'route/id/b', 'port': 'entry'},
                    'target': {'entity': 'route/id/b', 'port': 'exit'},
                },
            )
        with self.assertRaises(ValueError):
            transform(ROUTE, view, {'ids': ['route/id/c'], 'angle': 30})

    def test_near_port_snap_is_one_atomic_transform(self):
        view = model(ROUTE)
        candidate, _ = transform(
            ROUTE,
            view,
            {'ids': ['route/id/b', 'route/id/c'], 'delta': [0.5, 0, 0], 'snap_distance': 2},
        )
        final = model(candidate)
        self.assertEqual(final['connections']['open_connections'], [])
        self.assertEqual(by_id(final)['route/id/b']['entry'], by_id(view)['route/id/b']['entry'])

    def test_scenery_rotation_keeps_complete_assembly_pose(self):
        from ballance.route.layout import Layout
        from ballance.route.attachments import authored
        from ballance.route.spaces import part_id

        route = deepcopy(ROUTE)
        route['scenery'] = {
            'instances': [
                {'id': 'lamp', 'resource': 'LanternTower', 'parent': 'route/id/a', 'at': [0, 20, 0]}
            ]
        }
        view = model(route)
        moved, _ = transform(
            route, view, {'ids': ['scenery/id/lamp'], 'delta': [10, 3, 0], 'angle': 35}
        )
        layout = Layout(moved)
        layout.resource_scenery = moved['scenery']['instances']
        actors = authored(layout, {part_id(p): p for p in layout.parts})
        self.assertEqual(actors[0]['turn'], 35)
        expected = by_id(view)['scenery/id/lamp']['end']['position']
        for actual, original, delta in zip(actors[0]['anchor'], expected, [10, 3, 0]):
            self.assertAlmostEqual(actual, original + delta)
        self.assertEqual(moved['scenery']['rules']['scenery/id/lamp']['mode'], 'locked')

    def test_free_rotation_and_persistent_atomic_undo(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project.write_json(root / 'route.json', ROUTE)
            s = Session(root / 'route.json', root / 'edit', None, Nmo('nmo'))
            s.prepare()
            old = (s.current / 'rails1.obj').read_bytes()
            state = s.edit(
                'transform',
                'route/id/b',
                s.revision(),
                {'ids': ['route/id/b'], 'delta': [0, 10, 0], 'angle': 30},
            )
            self.assertIsNone(state['document_error'])
            self.assertNotEqual(old, (s.current / 'rails1.obj').read_bytes())
            self.assertTrue(state['model']['connections']['open_connections'])
            with self.assertRaises(ValueError):
                s.save(s.revision())
            s = Session(s.source, s.root, None, Nmo('nmo'))
            s.prepare()
            self.assertEqual(s.state()['undo_count'], 1)
            s.edit('undo', None, s.revision())
            self.assertEqual(s.route, ROUTE)
            self.assertEqual((s.current / 'rails1.obj').read_bytes(), old)
            self.assertEqual(read(s.source), ROUTE)


if __name__ == '__main__':
    unittest.main()
