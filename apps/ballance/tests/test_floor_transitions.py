"""Thickness, custom profiles and inclined platforms through geometry and native play."""

from collections import Counter
import math
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.obj import Mesh, read_obj
from ballance.kit.floors import WALL, RISE
from ballance.kit.rails import Ring
from ballance.kit.shapes import sweep, platform_texture
from ballance.route.build import build
from ballance.route.layout import Layout
from ballance.route.templates import mirror
from test_platforms import merge_course


PROFILE = [[-0.5, 0.7], [-0.2, 0.1], [0.1, 0], [0.5, 0.7]]


def platform(slope=0.1):
    return {
        'id': 'platform',
        'floor_polygon': {
            'outline': [[0, -8], [20, -8], [20, 8], [0, 8]],
            'entry': {'edge': 3, 'width': 8},
            'exit': {'edge': 1, 'width': 8},
            'depth': 6,
            'slope': slope,
        },
    }


def course(rail=False):
    approach = [
        {
            'id': 'shape',
            'connector': {'length': 15, 'to': {'width': 8, 'depth': 3, 'section': PROFILE}},
        },
        {
            'id': 'morph',
            'floor_path': {
                'segments': [{'line': [10, 0, 0]}],
                'width': 8,
                'section': PROFILE,
                'end_section': 'flat',
                'depth': [[0, 3], [0.4, 5], [1, 3]],
            },
        },
    ]
    if rail:
        approach = [{'id': 'rails', 'rail': 10}]
    return {
        'schema_version': 1,
        'start': {'heading': '+x'},
        'sectors': [
            [
                {'id': 'entry', 'road': 15},
                *approach,
                {
                    'id': 'inlet',
                    'connector': {
                        'endpoint': {'offset': [40, 0, 3], 'heading': [1, 0], 'slope': 0.1},
                        'to': {'kind': 'floor', 'width': 8, 'depth': 6, 'section': 'v'},
                    },
                },
                platform(),
                {
                    'id': 'outlet',
                    'connector': {
                        'endpoint': {'offset': [30, 0, 1.5], 'heading': [1, 0], 'slope': 0},
                        'to': {'width': 5, 'depth': WALL, 'section': 'v'},
                    },
                },
                {'id': 'exit', 'road': 15},
            ]
        ],
    }


def triangles(mesh):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / 'mesh.obj'
        mesh.write(path)
        return read_obj(path)


def boundary_edges(faces):
    """Count geometric edges after splitting collinear material-boundary vertices."""
    vertices = {point for face in faces for point in face.points}
    counts = Counter()
    for face in faces:
        for a, b in zip(face.points, face.points[1:] + face.points[:1]):
            delta = [y - x for x, y in zip(a, b)]
            length = sum(v * v for v in delta)
            if length < 1e-14:
                continue
            cuts = []
            for point in vertices:
                t = sum((p - x) * d for p, x, d in zip(point, a, delta)) / length
                if (
                    -1e-7 <= t <= 1 + 1e-7
                    and math.dist(point, [x + t * d for x, d in zip(a, delta)]) < 1e-6
                ):
                    cuts.append((t, point))
            cuts.sort()
            for (_, start), (_, end) in zip(cuts, cuts[1:]):
                counts[tuple(sorted((start, end)))] += 1
    return {edge: count for edge, count in counts.items() if count != 2}


class FloorTransitionTests(unittest.TestCase):
    def test_thickness_stations_and_distinct_profile_knots_reach_exact_ports(self):
        layout = Layout(course())
        transitions = {t['entity']['entity_id']: t for t in layout.transitions}
        inlet = transitions['route/id/inlet']
        self.assertEqual(inlet['from']['section']['depth'], 3)
        self.assertEqual(inlet['to']['section']['depth'], 6)
        self.assertEqual(transitions['route/id/outlet']['to']['section']['depth'], WALL)
        part = next(p for p in layout.parts if p.origin and p.origin.entity_id == 'route/id/morph')
        intervals = [args for function, _, *args in part.extras if function is sweep]
        self.assertIn(5, [args[-1] for args in intervals])
        self.assertEqual(intervals[0][8], 3)
        self.assertEqual(intervals[-1][-1], 3)
        self.assertFalse(layout.open_connections)

    def test_custom_sweep_is_closed_and_preserves_side_wall_thickness_and_uv(self):
        mesh = Mesh()
        profile = [tuple(p) for p in PROFILE]
        sweep(
            mesh,
            0,
            0,
            Ring(0, 0, 0, 1, 0),
            Ring(10, 2, 0, 1, 0),
            5,
            8,
            profile,
            profile,
            0,
            2,
            1,
            True,
            True,
            3,
        )
        faces = triangles(mesh)
        self.assertEqual(boundary_edges(faces), {})
        self.assertIn('Floor_Top_Border', {t.material for t in faces})
        sides = [t for t in faces if t.material == 'Floor_Side']
        points = {point for face in sides for point in face.points}
        self.assertIn((0, -0.3, 2.5), points)
        self.assertIn((10, -0.3, 4), points)
        self.assertTrue(any(abs(uv[0] - 0.6) < 1e-8 for t in sides for uv in t.uvs))
        for face in faces:
            self.assertTrue(all(math.isfinite(v) for uv in face.uvs for v in uv))

    def test_custom_border_keeps_physical_width_and_uv_seams_at_sample_boundaries(self):
        profile = [(-0.5, 0), (0, 0), (0.5, 0)]
        joints = []
        for start, end, wa, wb in ((0, 10, 5, 8), (10, 20, 8, 11)):
            mesh = Mesh()
            sweep(
                mesh,
                0,
                0,
                Ring(start, 0, 0, 1, 0),
                Ring(end, 0, 0, 1, 0),
                wa,
                wb,
                profile,
                profile,
                start / 5,
                end / 5,
            )
            faces = [t for t in triangles(mesh) if t.material.startswith('Floor_Top')]
            joints.append(
                {
                    (t.material, p, uv)
                    for t in faces
                    for p, uv in zip(t.points, t.uvs)
                    if abs(p[0] - 10) < 1e-7
                }
            )
            for face in faces:
                if face.material == 'Floor_Top_Border':
                    for point in face.points:
                        width = wa + (wb - wa) * (point[0] - start) / (end - start)
                        self.assertLessEqual(width / 2 - abs(point[2]), 0.750001)
        self.assertEqual(joints[0], joints[1])

    def test_inclined_platform_and_docks_agree_after_rotation_and_mirroring(self):
        for heading in ('+x', '+z', '-x', '-z'):
            for mirrored in (False, True):
                route = course(rail=True)
                route['start']['heading'] = heading
                if mirrored:
                    route['sectors'][0] = [mirror(p) for p in route['sectors'][0]]
                layout = Layout(route)
                with self.subTest(heading=heading, mirrored=mirrored):
                    platform = layout.platforms[0]
                    entry, exit = platform['ports']
                    self.assertAlmostEqual(exit['position'][1] - entry['position'][1], 3)
                    self.assertAlmostEqual(entry['slope'], 0.1)
                    self.assertAlmostEqual(exit['slope'], 0.1)
                    self.assertFalse(layout.open_connections)

    def test_plane_uvs_use_surface_distance_instead_of_horizontal_projection(self):
        vertices = [(0, 0, 0), (5, 1, 0), (0, 2, 5)]
        uvs = platform_texture(vertices, None)
        for i, j in ((0, 1), (0, 2), (1, 2)):
            self.assertAlmostEqual(
                math.dist(uvs[i], uvs[j]) * 5, math.dist(vertices[i], vertices[j])
            )
        mirrored = mirror(platform([0.1, 0.2]))
        self.assertEqual(mirrored['floor_polygon']['slope'], [0.1, -0.2])

    def test_cross_sloped_perforated_platform_has_only_its_two_open_mouths(self):
        route = course()
        piece = route['sectors'][0][4]
        piece['floor_polygon'].update(
            slope=[0.1, 0.03],
            supports=[[12, 2]],
            holes=[[[6, 2], [10, 2], [10, 4], [6, 4]]],
        )
        route['sectors'][0][3]['connector']['to']['section'] = [
            [-0.5, RISE - 0.12],
            [0, 0],
            [0.5, RISE + 0.12],
        ]
        layout = Layout(route)
        part = next(
            p for p in layout.parts if p.origin and p.origin.entity_id == 'route/id/platform'
        )
        mesh = Mesh()
        for function, (x, z), *args in part.extras:
            function(mesh, x, z, *args)
        faces = triangles(mesh)
        entry, exit = layout.platforms[0]['ports']
        boundaries = boundary_edges(faces)
        self.assertTrue(boundaries)
        for edge, count in boundaries.items():
            self.assertEqual(count, 1)
            self.assertTrue(
                any(
                    all(abs(p[0] - port['position'][0]) < 1e-6 for p in edge)
                    for port in (entry, exit)
                ),
                edge,
            )
        height = entry['position'][1] + 0.5 + RISE + 1.2 + 0.06 - 6
        self.assertTrue(any(abs(p[1] - height) < 1e-6 for p in layout.pillars))

    def test_named_sloped_port_reverses_profile_and_inherits_inward_grade(self):
        route = merge_course()
        for piece in route['sectors'][0]:
            if 'floor_polygon' in piece:
                piece['floor_polygon'].update(
                    slope=0.1,
                    outline=[[0, -10], [20, -5], [20, 10], [0, 10]],
                )
        # The oblique side edge has a nonzero outward grade and transverse tilt.
        cross_slope = 0.1 * 20 / math.hypot(20, 5)
        connector = {
            'id': 'curve',
            'connector': {
                'endpoint': {'platform': 'destination', 'port': 'side'},
                'to': {
                    'section': [
                        [-0.5, RISE + 2.5 * cross_slope],
                        [0, 0],
                        [0.5, RISE - 2.5 * cross_slope],
                    ]
                },
            },
        }
        route['sectors'][0][1]['branches'][0]['route'] = [connector]
        layout = Layout(route)
        joint = next(c for c in layout.connections if c.after.name == 'side')
        self.assertAlmostEqual(joint.after.slope, -0.1 * 5 / math.hypot(20, 5))
        self.assertAlmostEqual(joint.before.slope, joint.after.slope)
        self.assertFalse(layout.open_connections)
        connector['connector']['endpoint']['slope'] = 0
        with self.assertRaisesRegex(ValueError, 'named platform port'):
            Layout(route)

    def test_invalid_depths_and_platform_slopes_fail_with_source_identity(self):
        for depth in (0, 101, [[0, 2], [0.5, 0], [1, 2]]):
            route = course()
            route['sectors'][0][2]['floor_path']['depth'] = depth
            with self.subTest(depth=depth), self.assertRaisesRegex(ValueError, 'route/id/morph'):
                Layout(route)
        for slope in ([1], [0, float('nan')], True, [4, 4]):
            route = course()
            route['sectors'][0][4]['floor_polygon']['slope'] = slope
            with self.subTest(slope=slope), self.assertRaisesRegex(ValueError, 'route/id/platform'):
                Layout(route)


@unittest.skipUnless(
    os.environ.get('NMO_TEST_PHYSICS') == '1'
    and all(os.environ.get(k) for k in ('BALLANCE', 'BALLANCE_ELEMENTS', 'NMO_TEST_CLI')),
    'needs opt-in native physics and assets',
)
class FloorTransitionPhysicsTests(unittest.TestCase):
    def test_native_ball_crosses_custom_profiles_thickness_changes_and_sloped_platform(self):
        from ballance.core.nmo import Nmo
        from ballance.verify.play import play

        for rail in (False, True):
            with self.subTest(rail=rail), tempfile.TemporaryDirectory() as temporary:
                folder = Path(temporary)
                self.assertEqual(
                    build(course(rail), folder, Path(os.environ['BALLANCE_ELEMENTS']), 'Slopes'), 0
                )
                result = play(
                    folder, 5000, Nmo(os.environ['NMO_TEST_CLI']), Path(os.environ['BALLANCE'])
                )
                self.assertIsNotNone(result.finished, result.stalls)
                self.assertEqual(result.falls, [])


if __name__ == '__main__':
    unittest.main()
