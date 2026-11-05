"""Independent native authoring must survive export and parent transforms."""

from copy import deepcopy
import os
from pathlib import Path
import tempfile
import unittest

import support  # noqa: F401 - resolve the external toolchain before application imports
from ballance_editor.model import model, edited_route
from ballance_editor.transforms import transform, point
from ballance_editor.commands import change, view
from ballance_editor.session import Session, read
from ballance.core import project
from ballance.core.nmo import Nmo
from ballance.route.layout import Layout
from ballance.route.build import build
from ballance_editor.native import NativePreview

ROUTE = {
    'schema_version': 1,
    'scenery': {'finish_poles': []},
    'sectors': [
        [{'id': 'first', 'road': 20}],
        [
            {'id': 'entry', 'road': 10},
            {'id': 'assembly', 'resource': 'RotatingArm'},
            {'id': 'exit', 'road': 20},
        ],
    ],
}
OWNER = 'route/id/assembly'
ARM = OWNER + '/mechanism/P_Modul_17/0'
BRIDGE = OWNER + '/mechanism/P_Modul_25/0'


def entities(route):
    return {e['id']: e for e in model(route)['entities']}


class MechanismEditingTests(unittest.TestCase):
    def test_instance_pose_preserves_sibling_route_and_supports(self):
        original = entities(ROUTE)
        changed, selected = transform(
            ROUTE, model(ROUTE), {'ids': [BRIDGE], 'delta': [2, 3, 4], 'angle': 37}
        )
        updated = entities(changed)
        self.assertEqual(selected, BRIDGE)
        for identifier in (OWNER, ARM, 'route/id/exit'):
            self.assertEqual(original[identifier], updated[identifier])
        self.assertEqual(Layout(ROUTE).module_floors, Layout(changed).module_floors)
        for a, b, d in zip(
            updated[BRIDGE]['end']['position'], original[BRIDGE]['end']['position'], [2, 3, 4]
        ):
            self.assertAlmostEqual(a, b + d)
        self.assertAlmostEqual(
            updated[BRIDGE]['details']['yaw'], (original[BRIDGE]['details']['yaw'] + 37) % 360
        )
        self.assertEqual(model(changed)['connections'], model(ROUTE)['connections'])

    def test_parent_and_child_multiselect_applies_once_and_relative_pose_follows_parent(self):
        changed, _ = transform(
            ROUTE, model(ROUTE), {'ids': [BRIDGE], 'delta': [2, 3, 4], 'angle': 37}
        )
        before = entities(changed)[BRIDGE]
        operation = {'ids': [OWNER, BRIDGE], 'delta': [10, 4, -20], 'angle': 90, 'pivot': [0, 0, 0]}
        both, _ = transform(changed, model(changed), operation)
        parent, _ = transform(changed, model(changed), operation | {'ids': [OWNER]})
        a, b = entities(both)[BRIDGE], entities(parent)[BRIDGE]
        for actual, expected in zip(
            a['end']['position'], point(before['end']['position'], [0, 0, 0], [10, 4, -20], 90)
        ):
            self.assertAlmostEqual(actual, expected)
        for actual, expected in zip(a['end']['position'], b['end']['position']):
            self.assertAlmostEqual(actual, expected)
        self.assertAlmostEqual(a['details']['yaw'], b['details']['yaw'])

    def test_strict_parameters_and_invalid_draft_repair_handle(self):
        e = entities(ROUTE)[BRIDGE]
        for bad in (
            {'sector': 0},
            {'sector': 3},
            {'sector': True},
            {'yaw': float('nan')},
            {'offset': [0, 1]},
            {'speed': 20},
        ):
            candidate, _, _ = edited_route(ROUTE, e, bad, 'mechanism')
            with self.assertRaises(ValueError):
                model(candidate)
            draft, error = view(candidate, model(ROUTE))
            self.assertTrue(error)
            handle = next(x for x in draft['entities'] if x['id'] == BRIDGE)
            self.assertEqual(handle['piece'], bad)
            repaired, _, _ = edited_route(candidate, handle, e['piece'], 'mechanism')
            self.assertEqual(entities(repaired)[BRIDGE], e)
        candidate = deepcopy(ROUTE)
        candidate['mechanisms'] = {'instances': {'unknown': {}}}
        with self.assertRaisesRegex(ValueError, 'Unknown mechanism'):
            model(candidate)

    def test_duplicate_and_delete_preserve_instance_ownership(self):
        changed, _ = transform(ROUTE, model(ROUTE), {'ids': [BRIDGE], 'delta': [0, 2, 0]})
        duplicated, selected = change(changed, model(changed), 'duplicate', OWNER)
        duplicate = selected + '/mechanism/P_Modul_25/0'
        self.assertEqual(
            duplicated['mechanisms']['instances'][BRIDGE],
            duplicated['mechanisms']['instances'][duplicate],
        )
        deleted, _ = change(duplicated, view(duplicated, model(changed))[0], 'delete', OWNER)
        self.assertNotIn(BRIDGE, deleted['mechanisms']['instances'])
        self.assertIn(duplicate, entities(deleted))

    def test_export_activation_and_atomic_undo_reopen(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project.write_json(root / 'route.json', ROUTE)
            s = Session(root / 'route.json', root / 'edit', None, Nmo('nmo'))
            s.prepare()
            original = read(s.current / 'manifest.json')
            piece = entities(ROUTE)[BRIDGE]['piece'] | {'offset': [1, 2, 3], 'yaw': 23, 'sector': 1}
            s.edit('set', BRIDGE, s.revision(), piece, 'mechanism')
            self.assertIsNone(s.state()['document_error'])
            changed = read(s.current / 'manifest.json')
            old = {o['name']: o for o in original['scenes'][0]['objects']}
            new = {o['name']: o for o in changed['scenes'][0]['objects']}
            differences = [key for key in old if old[key] != new[key]]
            self.assertEqual(differences, ['P_Modul_25_01'])
            marker = new[differences[0]]
            self.assertIn('Sector_01', marker['groups'])
            self.assertNotIn('Sector_02', marker['groups'])
            pose = entities(s.route)[BRIDGE]['details']
            self.assertEqual(marker['transform']['position'], pose['position'])
            self.assertEqual(marker['transform']['rotation_euler_deg'][1], pose['yaw'])
            exported = next(
                i
                for i in read(s.current / 'spatial_constraints.json')['mechanisms']['instances']
                if i['id'] == BRIDGE
            )
            self.assertEqual(exported['position'], pose['position'])
            self.assertEqual(exported['sector'], 1)
            self.assertEqual(entities(s.route)[BRIDGE]['sector'], 2)
            legacy = read(s.before / 'spatial_constraints.json')
            for item in legacy['mechanisms']['instances']:
                item.pop('settings', None)
                item.pop('base_pose', None)
            project.write_json(s.before / 'spatial_constraints.json', legacy)
            s = Session(s.source, s.root, None, Nmo('nmo'))
            s.prepare()
            self.assertEqual(s.route['mechanisms']['instances'][BRIDGE], piece)
            s.edit('undo', None, s.revision())
            self.assertEqual(read(s.current / 'manifest.json'), original)
            self.assertEqual(read(s.source), ROUTE)

    @unittest.skipUnless(
        os.environ.get('BALLANCE_ELEMENTS') and os.environ.get('BALLANCE'), 'native assets required'
    )
    def test_native_materials_component_binding_and_motion_envelopes_follow_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            elements = Path(os.environ['BALLANCE_ELEMENTS'])
            build(ROUTE, root / 'before', elements, 'Test')
            changed, _ = transform(
                ROUTE, model(ROUTE), {'ids': [BRIDGE], 'delta': [2, 3, 4], 'angle': 23}
            )
            build(changed, root / 'after', elements, 'Test')
            resources = read(root / 'after/resources.json')
            self.assertEqual(resources['unbound_native_objects'], [])
            self.assertFalse(
                [
                    c
                    for i in resources['instances']
                    for c in i['components']
                    if c['status'] == 'missing'
                ]
            )
            preview = NativePreview(
                Path(os.environ['BALLANCE']), Nmo(os.environ['NMO_TEST_CLI']), root / 'cache'
            )
            before = {o['name']: o for o in preview.scene(root / 'before')[0]}
            after = {o['name']: o for o in preview.scene(root / 'after')[0]}
            moved = [name for name in before if before[name] != after[name]]
            self.assertTrue(moved)
            self.assertTrue(all(after[name]['instance'] == BRIDGE for name in moved))
            for name in moved:
                self.assertEqual(before[name]['triangles'], after[name]['triangles'])

            def boxes(folder):
                return [
                    v['box']
                    for v in read(folder / 'spatial_constraints.json')['occupancy']
                    if v['owner'] == BRIDGE
                ]

            self.assertTrue(boxes(root / 'before'))
            self.assertNotEqual(boxes(root / 'before'), boxes(root / 'after'))


if __name__ == '__main__':
    unittest.main()
