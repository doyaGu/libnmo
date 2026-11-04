"""Continuous motion, explicit interaction contracts and observed native poses."""

import csv
import json
import math
import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ballance.core.motion import rotation_boxes, rotate, corners, place, segment_face
from ballance.core.space import Space, Volume, bounds, inflate
from ballance.route import motions
from ballance.route.layout import Part, Layout
from ballance.route.spaces import route_space
from ballance.kit.rails import Ring
from ballance.verify.motions import check
from ballance.verify.motion_trace import inspect


def fixture(kind='P_Modul_17', yaw=0):
    return SimpleNamespace(
        parts=[Part(0, 1, 'module', modules=[(kind, 1, [30, 20, 50], yaw)])],
        sectors=[None],
        balloon=None,
        mechanism_settings={},
    )


class MotionTests(unittest.TestCase):
    def test_stop_certificate_requires_real_face_penetration(self):
        face = [(-2, 0, -2), (2, 0, -2), (0, 0, 2)]
        self.assertIsNotNone(segment_face((0, 1, 0), (0, -1, 0), face))
        self.assertIsNone(segment_face((1.9, 1, 1.9), (1.9, -1, 1.9), face))
        self.assertIsNone(segment_face((0, 0.01, 0), (0, -0.01, 0), face))
        self.assertIsNone(segment_face((0, 1, 0), (0, 2, 0), face))

    def test_beam_turn_gap_is_explicit_and_validated(self):
        for gap in (7, 81, True, '14', float('nan')):
            with self.assertRaisesRegex(ValueError, 'gap'):
                Layout(
                    {
                        'sectors': [
                            [
                                {'road': 10},
                                {'rail': 10},
                                {'beam': 20},
                                {'beam_turn': 'left', 'gap': gap},
                            ]
                        ]
                    }
                )
        base = {
            'sectors': [
                [
                    {'road': 10},
                    {'rail': 10},
                    {'beam': 20},
                    {'beam_turn': 'left'},
                    {'beam': 20},
                    {'rail': 15},
                    {'road': 10},
                ]
            ]
        }
        old = Layout(base)
        base['sectors'][0][3]['gap'] = 8
        self.assertEqual(old.points, Layout(base).points)

    def test_rotation_contains_unsampled_extrema_after_world_rotation(self):
        points = corners((0, 10, -0.2, 0.2, -0.1, 0.1))
        for yaw in (0, 37, -90):
            boxes = rotation_boxes(
                points, (0, 0, 0), (0, 1, 0), (4, 7, -12), yaw, 350, 710, step=90
            )
            for degrees in range(350, 711):
                for point in points:
                    p = place(
                        rotate(point, (0, 0, 0), (0, 1, 0), math.radians(degrees)),
                        (4, 7, -12),
                        yaw,
                    )
                    self.assertTrue(
                        any(
                            all(
                                b[2 * k] - 1e-7 <= v <= b[2 * k + 1] + 1e-7
                                for k, v in enumerate(p)
                            )
                            for b in boxes
                        )
                    )

    def test_invalid_motion_cannot_silently_reserve_nothing(self):
        for kwargs in (
            {'step': 0},
            {'start': 10, 'end': 0},
            {'start': 0, 'end': 361},
            {'yaw': float('nan')},
        ):
            with self.assertRaises(ValueError):
                rotation_boxes([(1, 0, 0)], (0, 0, 0), (0, 1, 0), **kwargs)
        with self.assertRaises(ValueError):
            rotation_boxes([(1, 0, 0)], (0, 0, 0), (0, 2, 0))

    def test_sphere_probe_rejects_corner_of_reach_box(self):
        space = Space()
        space.add(Volume('outside', 'rail', (7, 8, -1, 1, 7, 8)))
        space.add(
            Volume(
                'inside',
                'rail',
                (-11, 11, -1, 1, -1, 1),
                segment=((-10, 0, 0), (10, 0, 0)),
                radius=1,
            )
        )
        sphere = Volume('sack', 'reach', (-9, 9, -9, 9, -9, 9), centre=(0, 0, 0), radius=9)
        self.assertEqual([h['owner'] for h in space.query(sphere)], ['inside'])
        with self.assertRaises(ValueError):
            Volume('a', 'b', (-1, 1, -1, 1, -1, 1), centre=(0, 0, 0), radius=2)

    def test_motion_state_names_are_instance_scoped(self):
        layout = fixture()
        instance = motions.instances(layout)[0]
        space = route_space(layout)
        # Arm's initial pose is along X. Its rotated pose reaches Z=60.
        box = (29.9, 30.1, 16.5, 17.5, 59.9, 60.1)
        self.assertEqual(space.query([box], states={instance['id'] + '/ready'}), [])
        self.assertTrue(space.query([box], states={instance['id'] + '/moving'}))
        self.assertEqual(space.query([box], states={'another/moving'}), [])
        self.assertEqual(space.query([box], states={instance['id'] + '/inactive'}), [])

    def test_foreign_rail_in_sweep_is_not_hidden_by_adjacency(self):
        layout = fixture()
        layout.parts.append(
            Part(
                1,
                1,
                'rail',
                after=0,
                segments=[(Ring(28, 17, 60, 1, 0), Ring(32, 17, 60, 1, 0))],
            )
        )
        report = check(layout, None)
        self.assertEqual(report['status'], 'review')
        self.assertTrue(
            any(
                f['phase'] == 'moving' and f['contact']['kind'] == 'rail'
                for f in report['findings']
            )
        )

    def test_interaction_contract_is_exact_and_remains_in_report(self):
        layout = fixture()
        layout.parts.append(
            Part(1, 1, 'rail', segments=[(Ring(28, 17, 60, 1, 0), Ring(32, 17, 60, 1, 0))])
        )
        identifier = motions.instances(layout)[0]['id']
        layout.mechanism_settings = {
            'interactions': [
                {
                    'mechanism': identifier,
                    'target': 'sector/1/rail/1',
                    'contacts': ['ball clearance'],
                    'phases': ['moving'],
                    'reason': 'a timed obstacle',
                }
            ]
        }
        r = check(layout, None)
        self.assertTrue(r['expected_interactions'])
        self.assertTrue(any(f['contact']['kind'] == 'rail' for f in r['findings']))
        layout.mechanism_settings['interactions'][0]['target'] = 'missing'
        with self.assertRaisesRegex(ValueError, 'unknown'):
            motions.describe(layout)

    def test_unmodeled_mechanisms_and_changed_templates_are_visible(self):
        r = check(fixture('P_Modul_99'), None)
        self.assertEqual(r['status'], 'review')
        self.assertEqual(len(r['unsupported']), 1)
        with tempfile.TemporaryDirectory() as tmp:
            r = check(fixture(), None, Path(tmp))
            self.assertEqual(r['status'], 'failed')
            self.assertEqual(r['template_mismatches'], ['P_Modul_17'])

    def test_recipe_bodies_are_inside_initial_and_full_motion_reservations(self):
        for kind, recipe in motions.catalog().items():
            layout = fixture(kind, 123)
            instance = motions.instances(layout)[0]
            space = route_space(layout)
            for body in recipe['bodies'].values():
                for p in body['points']:
                    point = place(p, instance['position'], instance['yaw'])
                    self.assertTrue(space.query([inflate(bounds([point]), 1e-6)]), kind)

    def test_trace_records_displacement_and_detects_envelope_escape(self):
        layout = fixture()
        recipe = motions.catalog()['P_Modul_17']
        name = recipe['body']
        m = recipe['bodies'][name]['matrix'][:]
        m[12] += 30
        m[13] += 20
        m[14] += 50
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'trace.csv'
            with path.open('w', newline='') as stream:
                writer = csv.writer(stream)
                writer.writerow(['frame', 'id', 'name', *[f'm{k}' for k in range(16)]])
                writer.writerow([1, 42, name, *m])
                m[12] += 100
                writer.writerow([2, 42, name, *m])
            r = inspect(path, layout, 2)
            self.assertEqual(r['observed_instances'], 1)
            self.assertEqual(r['bodies'][0]['first_displaced_frame'], 2)
            self.assertEqual(r['envelope_escapes'][0]['frame'], 2)
            self.assertEqual(inspect(path, layout, 1)['envelope_escapes'], [])

    def test_missing_and_malformed_trace_do_not_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'trace.csv'
            self.assertEqual(inspect(path, fixture())['status'], 'unavailable')
            path.write_text('frame,x,y,z\n')
            with self.assertRaisesRegex(ValueError, 'header'):
                inspect(path, fixture())


@unittest.skipUnless(
    os.environ.get('NMO_TEST_PHYSICS') == '1' and os.environ.get('BALLANCE'),
    'needs the local native Ballance templates',
)
class NativeMotionTests(unittest.TestCase):
    def test_switch_stop_geometry_prevents_reverse_sweep_false_alarm(self):
        from ballance.route.assets import Assets
        from ballance.core.motion import place

        folder = Path(os.environ['BALLANCE_ELEMENTS'])
        for yaw in (0, 90, 270):
            layout = fixture('P_Modul_37', yaw)
            layout.parts[0].module_floors = [('P_Modul_37', 1, [30, 20, 50], yaw)]
            a, b = [place(p, (30, 20, 50), yaw) for p in ((8.73, 0.43, 0), (28.73, 7.44, 0))]
            dx, dz = b[0] - a[0], b[2] - a[2]
            length = math.hypot(dx, dz)
            layout.parts.append(
                Part(
                    1,
                    1,
                    'rail',
                    segments=[
                        (Ring(*a, dx / length, dz / length), Ring(*b, dx / length, dz / length))
                    ],
                )
            )
            report = check(layout, Assets.existing(folder))
            self.assertEqual(report['findings'], [], yaw)
            # Removing the actual stop surfaces restores the full sweep risk.
            layout.parts[0].module_floors = []
            self.assertTrue(check(layout, Assets.existing(folder))['findings'])

    def test_measured_catalog_matches_native_templates(self):
        from ballance.core.nmo import Nmo
        from ballance.extract.motion import measure

        actual = measure(Path(os.environ['BALLANCE']), Nmo(os.environ.get('NMO_TEST_CLI')))
        self.assertEqual(json.loads(json.dumps(actual))['recipes'], motions.catalog())


if __name__ == '__main__':
    unittest.main()
