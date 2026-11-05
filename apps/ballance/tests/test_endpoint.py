"""Endpoint connections through the public route compiler and native adapters."""

import copy
import math
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.path import sample_path
from ballance.route.layout import Layout
from ballance.route.build import build
from test_platforms import merge_course


def connector(offset=(60, 20, 8), heading=(1, 0), **options):
    return {
        'id': 'link',
        'connector': {
            'endpoint': {'offset': list(offset), 'heading': list(heading)},
            'to': {},
            **options,
        },
    }


def course(link=None, destination=None):
    return {
        'schema_version': 1,
        'start': {'heading': '+x'},
        'sectors': [
            [
                {'id': 'entry', 'road': 10},
                {'id': 'rails', 'rail': 10},
                link or connector(),
                destination or {'id': 'destination', 'road': 15},
                {'id': 'exit', 'road': 10},
            ]
        ],
    }


class EndpointTests(unittest.TestCase):
    def test_offset_rise_and_rotation_end_exactly_at_the_requested_pose(self):
        for heading in ('+x', '+z', '-x', '-z'):
            for mirrored in (False, True):
                route = course()
                route['start']['heading'] = heading
                route['definitions'] = {'link': {'pieces': [connector()]}}
                route['sectors'][0][2] = {'id': 'instance', 'use': 'link', 'mirror': mirrored}
                layout = Layout(route)
                transition = layout.transitions[0]
                before, after = transition['from'], transition['to']
                dx, dz = before['heading']
                right = -20 if mirrored else 20
                expected = [
                    before['position'][0] + 60 * dx + right * dz,
                    before['position'][1] + 8,
                    before['position'][2] + 60 * dz - right * dx,
                ]
                self.assertLess(math.dist(after['position'], expected), 1e-8)
                self.assertEqual(after['heading'], before['heading'])
                self.assertTrue(after['smooth'])
                self.assertEqual(after['slope'], 0)
                self.assertFalse(layout.open_connections)

    def test_placed_platform_and_native_module_follow_the_same_connection(self):
        platform = {
            'id': 'destination',
            'floor_polygon': {
                'outline': [[0, -5], [20, -5], [20, 5], [0, 5]],
                'entry': {'edge': 3},
                'exit': {'edge': 1},
            },
        }
        for destination in (platform, {'id': 'destination', 'module': 'P_Modul_25'}):
            route = course(destination=destination)
            pose = {'position': [95, -8, -24], 'heading': [1, 0]}
            route['placements'] = {'route/id/destination': pose}
            route['sectors'][0][2]['connector']['endpoint'] = {'placement': 'route/id/destination'}
            route['sectors'][0][2]['connector']['to'] = {'kind': 'floor'}
            layout = Layout(route)
            self.assertEqual(layout.entry_poses['route/id/destination'], pose)
            self.assertFalse(layout.open_connections)
            joint = next(c for c in layout.connections if c.after.owner == 'route/id/destination')
            self.assertEqual(joint.kind, 'floor-seam')
            self.assertTrue(any(c.kind == 'rail-floor-dock' for c in layout.connections))
            self.assertLess(math.dist(joint.before.position, pose['position']), 1e-8)
            # Moving only the destination updates the connecting geometry.
            route['placements']['route/id/destination']['position'][2] -= 12
            moved = Layout(route)
            self.assertFalse(moved.open_connections)
            self.assertNotEqual(layout.transitions[0]['path'], moved.transitions[0]['path'])

    def test_named_platform_port_resolves_inward_for_a_branch_merge(self):
        route = merge_course()
        branch = route['sectors'][0][1]['branches'][0]
        branch['route'][0] = connector()
        branch['route'][0]['connector']['endpoint'] = {'platform': 'destination', 'port': 'side'}
        layout = Layout(route)
        joint = next(c for c in layout.connections if c.after.name == 'side')
        self.assertLess(math.dist(joint.before.position, joint.after.position), 1e-8)
        self.assertEqual(joint.before.heading, joint.after.heading)
        self.assertFalse(layout.open_connections)

    def test_end_slopes_are_inherited_and_checked_against_the_destination(self):
        route = course()
        route['sectors'][0][1] = {'id': 'ramp', 'rail_path': {'segments': [{'line': [20, 0, 4]}]}}
        route['sectors'][0][2]['connector']['endpoint']['slope'] = -0.1
        route['sectors'][0][3] = {
            'id': 'downhill',
            'rail_path': {'segments': [{'line': [20, 0, -2]}]},
        }
        layout = Layout(route)
        samples = sample_path(layout.transitions[0]['path'])
        self.assertAlmostEqual(samples[0].slope, 0.2)
        self.assertAlmostEqual(samples[-1].slope, -0.1)
        route['sectors'][0][3]['rail_path']['segments'][0]['line'][2] = 0
        with self.assertRaisesRegex(ValueError, 'slope mismatch'):
            Layout(route)
        route['sectors'][0][2]['connector']['start_slope'] = 0
        with self.assertRaisesRegex(ValueError, 'incoming path'):
            Layout(route)
        route = course()
        route['sectors'][0][1] = {
            'id': 'wave',
            'rail_path': {'segments': [{'wave': {'length': 60, 'amplitude': 3}}]},
        }
        with self.assertRaisesRegex(ValueError, 'incoming slope is unknown'):
            Layout(route)
        route['sectors'][0][2]['connector']['start_slope'] = 0
        self.assertFalse(Layout(route).open_connections)
        route = course()
        route['sectors'][0][1]['drop'] = 2
        self.assertAlmostEqual(sample_path(Layout(route).transitions[0]['path'])[0].slope, -0.2)
        route['sectors'][0][3]['drop'] = 2
        with self.assertRaisesRegex(ValueError, 'slope mismatch'):
            Layout(route)

    def test_dock_closes_exposed_walls_but_keeps_internal_seams_open(self):
        from ballance.core.obj import read_obj
        from ballance.verify.geometry import check
        from ballance.view.scene import load_project

        route = course(connector(to={'kind': 'floor'}))
        layout = Layout(route)
        dock = next(
            c
            for c in layout.connections
            if c.kind == 'rail-floor-dock' and c.after.owner == 'route/id/link'
        )
        end = layout.transitions[0]['to']['position']
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            self.assertEqual(build(route, folder, None, 'Dock'), 0)
            findings = check(load_project(folder))
            self.assertEqual(findings, [])
            triangles = read_obj(folder / 'sector1.obj')
            caps = [
                t
                for t in triangles
                if t.material == 'Floor_Side'
                and all(abs(p[0] - dock.after.position[0]) < 1e-5 for p in t.points)
            ]
            self.assertTrue(caps)
            internal_caps = [
                t
                for t in triangles
                if t.material == 'Floor_Side' and all(abs(p[0] - end[0]) < 1e-5 for p in t.points)
            ]
            self.assertEqual(internal_caps, [])

    def test_curvature_and_grade_limits_do_not_depend_on_mesh_sampling(self):
        for step in (0.1, 10):
            with self.assertRaisesRegex(ValueError, 'no endpoint connector'):
                Layout(course(connector((10, 10, 0), handles=[1, 1], min_radius=30, step=step)))
            with self.assertRaisesRegex(ValueError, 'no endpoint connector'):
                Layout(course(connector((20, 0, 30), max_grade=0.1, step=step)))
        # Inspect a successful curve densely with independent finite differences.
        layout = Layout(course(connector(min_radius=20, max_grade=0.3)))
        controls = [(0, 0, 0), *layout.transitions[0]['path']['segments'][0]['bezier']]

        def point(t):
            weights = [(1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t * t * (1 - t), t**3]
            return [sum(w * p[k] for w, p in zip(weights, controls)) for k in range(3)]

        points = [point(i / 2000) for i in range(2001)]
        for a, b, c in zip(points, points[1:], points[2:]):
            ab = [b[k] - a[k] for k in range(3)]
            bc = [c[k] - b[k] for k in range(3)]
            self.assertLessEqual(abs(ab[2]) / math.hypot(*ab[:2]), 0.300001)
            cross = abs(ab[0] * bc[1] - ab[1] * bc[0])
            if cross > 1e-14:
                radius = (
                    math.hypot(*ab[:2])
                    * math.hypot(*bc[:2])
                    * math.dist(a[:2], c[:2])
                    / (2 * cross)
                )
                self.assertGreaterEqual(radius, 19.999)

    def test_invalid_and_unresolved_constraints_fail_with_source_identity(self):
        cases = [
            {'endpoint': {'offset': [30, 0, 0], 'heading': [2, 0]}},
            {'endpoint': {'placement': 'missing'}},
            {'endpoint': {'platform': 'missing', 'port': 'side'}},
            {'endpoint': {'offset': [0, 0, 10], 'heading': [1, 0]}},
            {'endpoint': {'offset': [30, 0, 0], 'heading': [1, 0], 'typo': 1}},
            {'max_grade': True},
            {'min_radius': float('nan')},
            {'handles': [0, 1]},
            {'length': 20},
        ]
        for options in cases:
            with self.subTest(options=options), self.assertRaisesRegex(ValueError, 'route/id/link'):
                Layout(course(connector(**copy.deepcopy(options))))


@unittest.skipUnless(
    os.environ.get('NMO_TEST_PHYSICS') == '1'
    and all(os.environ.get(key) for key in ('BALLANCE', 'BALLANCE_ELEMENTS', 'NMO_TEST_CLI')),
    'needs opt-in native physics and assets',
)
class EndpointPhysicsTests(unittest.TestCase):
    def test_offset_and_height_connections_reach_platform_and_native_drawbridge(self):
        from ballance.core.nmo import Nmo
        from ballance.verify.play import play

        platform = {
            'id': 'destination',
            'floor_polygon': {
                'outline': [[0, -5], [20, -5], [20, 5], [0, 5]],
                'entry': {'edge': 3},
                'exit': {'edge': 1},
            },
        }
        for destination in (platform, {'id': 'destination', 'module': 'P_Modul_25'}):
            route = course(
                connector((70, 15, -5), to={'kind': 'floor'}, max_grade=0.3), destination
            )
            with (
                self.subTest(destination=destination),
                tempfile.TemporaryDirectory(prefix='endpoint-physics-') as tmp,
            ):
                folder = Path(tmp)
                self.assertEqual(
                    build(route, folder, Path(os.environ['BALLANCE_ELEMENTS']), 'Endpoint'), 0
                )
                result = play(
                    folder, 4000, Nmo(os.environ['NMO_TEST_CLI']), Path(os.environ['BALLANCE'])
                )
                self.assertIsNotNone(result.finished, result.stalls)
                self.assertEqual(result.falls, [])


if __name__ == '__main__':
    unittest.main()
