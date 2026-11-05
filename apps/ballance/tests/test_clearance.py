"""Real mapping failures: holes, low ceilings, sloped floors and swept gates."""

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.obj import Triangle
from ballance.core.space import Volume
from ballance.core.surface import Surfaces, segment_triangle
from ballance.route.passages import compile_passages
from ballance.verify.clearance import check
from ballance.view.scene import SceneObject


def floor(name, x0=-10, x1=10, z0=-10, z1=10, y=0):
    points = [(x0, y, z0), (x0, y, z1), (x1, y, z1), (x1, y, z0)]
    return SceneObject(
        name,
        ['Phys_Floors'],
        [
            Triangle(None, [points[i] for i in indices], None, None)
            for indices in ((0, 1, 2), (0, 2, 3))
        ],
        (0, 0, 0),
    )


def layout(kind='floor', points=None, **options):
    value = SimpleNamespace(
        parts=[],
        balloon=None,
        mechanism_instances={},
        mechanism_settings={},
        entry_poses={'route/test': {'position': [0, 0, 0], 'heading': [1, 0]}},
    )
    value.passages = compile_passages(
        [
            {
                'id': 'crossing',
                'owner': 'route/test',
                'kind': kind,
                'points': points or [[-6, 0, 2], [6, 0, 2]],
                **options,
            }
        ],
        value,
    )
    return value


class ClearanceTests(unittest.TestCase):
    def test_passage_contacts_keep_distinct_ids_within_one_owner(self):
        from ballance.core.space import Space
        from ballance.route.passages import volumes

        corridor = layout()
        first = corridor.passages[0]
        second = first | {'id': 'exit'}
        space = Space()
        for volume in volumes([first, second]):
            space.add(volume)
        contacts = space.query([(-1, 1, 0, 4, -1, 1)])
        self.assertEqual({c['passage'] for c in contacts}, {'crossing', 'exit'})

    def test_support_fit_does_not_treat_a_low_ceiling_as_ground(self):
        ground = floor('floor')
        roof = floor('roof', y=3)
        surfaces = Surfaces((o.name, t.points) for o in (ground, roof) for t in o.triangles)
        self.assertFalse(surfaces.supporting_contacts((-2, 2, 0), (2, 2, 0), 2))

    def test_low_ceiling_and_clear_overpass(self):
        corridor = layout(half_width=1)
        self.assertEqual(
            check([floor('ground'), floor('overpass', y=5)], corridor)['status'], 'passed'
        )
        report = check([floor('ground'), floor('overpass', y=3.5)], corridor)
        self.assertEqual(report['status'], 'failed')
        self.assertEqual({f['obstacle'] for f in report['findings']}, {'overpass'})

    def test_continuous_capsule_catches_thin_obstacle_between_endpoints(self):
        triangle = [(0, -5, -5), (0, 5, -5), (0, 0, 5)]
        self.assertEqual(segment_triangle((-10, 0, 0), (10, 0, 0), triangle), 0)
        self.assertAlmostEqual(segment_triangle((-10, 0, 7), (10, 0, 7), triangle), 2)
        surfaces = Surfaces([('wall', triangle)])
        self.assertIn('wall', surfaces.clearance((-10, 0, 0), (10, 0, 0), 2))

    def test_triangle_hole_is_not_a_filled_bounding_box(self):
        scene = [floor('left', x1=-3), floor('right', x0=3)]
        report = check(scene, layout())
        self.assertEqual([f['rule'] for f in report['findings']], ['floor support'])
        self.assertEqual(check(scene + [floor('lower', y=-8)], layout())['status'], 'failed')

    def test_side_wall_blocks_ball_even_with_clear_centre_line(self):
        wall = SceneObject(
            'wall',
            ['Phys_FloorStopper'],
            [Triangle(None, [(-8, 0, 1.5), (8, 0, 1.5), (0, 8, 1.5)], None, None)],
            (0, 0, 0),
        )
        report = check([floor('ground'), wall], layout())
        self.assertEqual(report['status'], 'failed')
        self.assertEqual(report['findings'][0]['obstacle'], 'wall')

    def test_landing_requires_correct_layer_and_recovery_disc(self):
        corridor = layout('landing', [[0, 0, 12], [0, 0, 2]], landing_radius=3)
        self.assertEqual(check([floor('landing')], corridor)['status'], 'passed')
        self.assertEqual(check([floor('lower', y=-5)], corridor)['status'], 'failed')
        self.assertEqual(check([floor('ledge', x0=-1, x1=1)], corridor)['status'], 'failed')
        report = check([floor('landing'), floor('intermediate', y=6)], corridor)
        self.assertEqual(report['status'], 'failed')
        self.assertIn('intermediate', {f.get('obstacle') for f in report['findings']})

    def test_sloped_support_and_degenerate_triangles(self):
        triangle = [(0, 0, 0), (0, 0, 10), (10, 5, 0)]
        surface = Surfaces([('ramp', triangle)])
        self.assertEqual(surface.support((2, 1, 2)), 'ramp')
        self.assertIsNone(surface.support((2, 3, 2)))
        self.assertEqual(segment_triangle((0, 3, 0), (0, 4, 0), [(0, 0, 0)] * 3), 3)

    def test_owner_local_coordinates_follow_rotated_placement(self):
        value = layout()
        value.entry_poses['route/test'] = {'position': [100, 20, 30], 'heading': [0, -1]}
        result = compile_passages(
            [
                {
                    'id': 'p',
                    'owner': 'route/test',
                    'kind': 'floor',
                    'points': [[0, 2, 2], [10, 2, 2]],
                }
            ],
            value,
        )
        self.assertEqual(result[0]['points'], [(98, 22, 30), (98, 22, 20)])

    def test_invalid_contracts_fail_instead_of_disappearing(self):
        for options in (
            {'half_width': -1},
            {'half_width': float('nan')},
            {'landing_radius': 1},
            {'unknown': True},
        ):
            with self.assertRaises(ValueError):
                layout(**options)
        with self.assertRaises(ValueError):
            layout('landing', [[0, 0, 2], [0, 0, 5]])
        with self.assertRaises(ValueError):
            layout(points=[[0, 0, 2], [0, 0, 2]])

    def test_owning_mechanism_motion_is_checked_and_intentions_are_explicit(self):
        corridor = layout()
        instance = {'id': 'gate', 'kind': 'test', 'parent': 'route/test'}
        motion = Volume(
            'gate', 'mechanism sweep', (-1, 1, 0, 5, -1, 1), 'moving', source={'phase': 'moving'}
        )
        with (
            patch('ballance.verify.clearance.motions.instances', return_value=[instance]),
            patch('ballance.verify.clearance.motions.volumes', return_value=[motion]),
            patch('ballance.verify.clearance.motions.unbounded', return_value=[]),
        ):
            report = check([floor('ground')], corridor)
            self.assertEqual(report['status'], 'review')
            self.assertEqual(len(report['motion_contacts']), 1)
            corridor.mechanism_settings = {
                'interactions': [
                    {
                        'mechanism': 'gate',
                        'target': 'route/test',
                        'contacts': ['floor passage'],
                        'phases': ['moving'],
                        'reason': 'Wait for the gate to open.',
                    }
                ]
            }
            corridor.parts = [
                SimpleNamespace(
                    origin=SimpleNamespace(entity_id='route/test'),
                    segments=[],
                    sector=1,
                    index=0,
                    branch=0,
                    what='road',
                )
            ]
            corridor.parts[0].origin.data = lambda: {}
            report = check([floor('ground')], corridor)
            self.assertEqual(report['status'], 'passed')
            self.assertEqual(len(report['expected_interactions']), 1)
