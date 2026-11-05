"""Geometry and route contracts for authored paths and platform outlines."""

import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.path import sample_path, stations, interpolate, include_stations
from ballance.core.obj import Mesh, read_obj
from ballance.kit.rails import Ring, Beam, _tube
from ballance.kit.shapes import polygon, triangulate, cross, inside
from ballance.route.build import build
from ballance.route.layout import Layout, Part
from ballance.route.templates import expand, mirror


def route(*pieces, **kwargs):
    return {
        'start': {'heading': '+x'},
        'sectors': [[{'road': 10}, *pieces, {'road': 10}]],
        **kwargs,
    }


class ClearanceTests(unittest.TestCase):
    def test_single_beam_needs_more_headroom_than_double_rail(self):
        from types import SimpleNamespace
        from ballance.route.clashes import clashes
        from ballance.route.assets import Assets

        with tempfile.TemporaryDirectory() as tmp:
            assets = Assets(Path(tmp), [], [], None)
            upper = Part(
                0, 1, 'rail', index=1, segments=[(Ring(0, 4.2, -10, 0, 1), Ring(0, 4.2, 10, 0, 1))]
            )
            for gauge, expected in ((0, True), (3.8, False)):
                lower = Part(
                    1,
                    2,
                    'beam_path' if gauge == 0 else 'rail',
                    index=1,
                    segments=[
                        (Ring(-10, 0, 0, 1, 0, gauge=gauge), Ring(10, 0, 0, 1, 0, gauge=gauge))
                    ],
                )
                self.assertEqual(
                    bool(clashes(SimpleNamespace(parts=[upper, lower]), assets)), expected
                )
            upper.segments = [(Ring(0, 6, -10, 0, 1), Ring(0, 6, 10, 0, 1))]
            lower.segments = [(Ring(-10, 0, 0, 1, 0, gauge=0), Ring(10, 0, 0, 1, 0, gauge=0))]
            self.assertEqual(clashes(SimpleNamespace(parts=[upper, lower]), assets), [])


class PathTests(unittest.TestCase):
    def test_bezier_samples_an_s_curve_even_when_midpoint_is_on_chord(self):
        samples = sample_path(
            {
                'segments': [
                    {'bezier': [[5, 0, 0], [5, 10, 4], [10, 10, 4]]},
                    {'bezier': [[15, 10, 4], [15, 0, 0], [20, 0, 0]]},
                ]
            }
        )
        self.assertEqual(samples[0].point, (0, 0, 0))
        self.assertEqual(samples[-1].point, (20, 0, 0))
        self.assertGreater(max(s.point[1] for s in samples), 9.9)
        self.assertGreater(samples[-1].distance, 25)
        self.assertTrue(
            all(math.dist(a.point, b.point) <= 2.001 for a, b in zip(samples, samples[1:]))
        )
        self.assertTrue(all(abs(math.hypot(*s.direction) - 1) < 1e-10 for s in samples))

    def test_signed_arcs_and_helix(self):
        for angle, end in ((90, (10, 10, 5)), (-90, (10, -10, 5)), (360, (0, 0, 5))):
            with self.subTest(angle=angle):
                samples = sample_path(
                    {'segments': [{'arc': {'radius': 10, 'angle': angle, 'rise': 5}}]}
                )
                self.assertLess(math.dist(samples[-1].point, end), 1e-10)
                self.assertGreater(samples[1].point[0], 0)
                self.assertAlmostEqual(
                    samples[-1].distance, math.hypot(10 * math.radians(angle), 5), delta=0.1
                )

    def test_invalid_paths_fail_before_mesh_generation(self):
        specs = [
            {'segments': []},
            {'segments': [{'line': [0, 0, 4]}]},
            {'segments': [{'line': [5, 0, 0]}, {'line': [5, 5, 0]}]},
            {'segments': [{'bezier': [[0, 0, 0], [5, 0, 0], [10, 0, 0]]}]},
            {'segments': [{'arc': {'radius': -1, 'angle': 90}}]},
            {'segments': [{'line': [float('nan'), 0, 0]}]},
            {'segments': [{'line': [10, 0, 0]}], 'step': 0},
        ]
        for spec in specs:
            with self.subTest(spec=spec), self.assertRaises(ValueError):
                sample_path(spec)

    def test_keyframes_require_ordered_bounded_values(self):
        keys = stations([[0, 5], [0.4, 10], [1, 5]], 'width', 1, 20)
        self.assertAlmostEqual(interpolate(keys, 0.2), 7.5)
        for bad in ([[0.1, 5], [1, 5]], [[0, 5], [0, 6], [1, 5]], -1, float('inf')):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                stations(bad, 'width', 1, 20)

    def test_bank_rotates_tubes_and_normals_without_changing_gauge(self):
        rings = [Ring(x, 0, 0, 1, 0, 30, 4, 0.5) for x in (0, 10)]
        right, up = rings[0].axes()
        self.assertAlmostEqual(sum(a * b for a, b in zip(right, up)), 0)
        self.assertAlmostEqual(right[1] * 4, 2)
        mesh = Mesh()
        _tube(mesh, rings, 1)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'rail.obj'
            mesh.write(path)
            tris = read_obj(path)
        self.assertTrue(tris)
        for tri in tris:
            self.assertTrue(
                all(abs(math.sqrt(sum(v * v for v in n)) - 1) < 1e-5 for n in tri.normals)
            )

    def test_style_peak_between_samples_is_preserved(self):
        samples = sample_path({'segments': [{'line': [10, 0, 0]}]})
        sampled = include_stations(samples, [0.01, 0.02])
        self.assertTrue(any(abs(s.distance - 0.1) < 1e-10 for s in sampled))
        self.assertTrue(any(abs(s.distance - 0.2) < 1e-10 for s in sampled))
        merged = include_stations(samples, [0.2 + 1e-16, 0.4 - 1e-16])
        self.assertEqual(len(merged), len(samples))
        self.assertTrue(all(b.distance > a.distance for a, b in zip(merged, merged[1:])))


class PolygonTests(unittest.TestCase):
    def test_concave_triangulation_preserves_notch_in_both_windings(self):
        shape = [[0, 0], [6, 0], [6, 2], [2, 2], [2, 6], [0, 6]]
        for values in (shape, shape[::-1]):
            points = polygon(values)
            triangles = triangulate(points)
            area = sum(abs(cross(*(points[i] for i in tri))) / 2 for tri in triangles)
            self.assertAlmostEqual(area, 20)
            for tri in triangles:
                centre = tuple(sum(points[i][k] for i in tri) / 3 for k in (0, 1))
                self.assertTrue(inside(centre, points))

    def test_self_crossing_and_repeated_vertices_are_rejected(self):
        for points in (
            [[0, 0], [5, 5], [0, 5], [5, 0]],
            [[0, 0], [5, 0], [5, 0], [0, 5]],
            [[0, 0], [5, 0], [2, 0], [2, 5], [0, 5]],
        ):
            with self.assertRaises(ValueError):
                polygon(points)

    def test_collinear_vertices_keep_edge_indices_without_zero_area_faces(self):
        outline = polygon([[0, 0], [3, 0], [6, 0], [6, 6], [3, 6], [0, 6]])
        areas = [abs(cross(*(outline[k] for k in t))) / 2 for t in triangulate(outline)]
        self.assertTrue(all(a > 0 for a in areas))
        self.assertEqual(sum(areas), 36)


class RouteTests(unittest.TestCase):
    def test_curved_single_beam_retains_original_transition_heights(self):
        course = route(
            {'rail': 10},
            {'beam_path': {'segments': [{'wave': {'length': 80, 'amplitude': 5}}]}},
            {'rail': 10},
        )
        layout = Layout(course)
        runs = layout.sectors[0].rails.runs
        self.assertIsInstance(runs[1], Beam)
        self.assertGreater(max(r.z for r in runs[1]), 4.99)
        self.assertLess(min(r.z for r in runs[1]), -4.99)
        self.assertAlmostEqual(runs[0][-1].y, runs[-1][-1].y)
        self.assertTrue(all(r.gauge == 0 for r in runs[1][2:]))
        with self.assertRaisesRegex(ValueError, 'starts from'):
            Layout(route({'beam_path': {'segments': [{'line': [20, 0, 0]}]}}))

    def test_free_paths_chain_at_diagonal_then_rejoin_grid(self):
        layout = Layout(
            route(
                {'floor_path': {'segments': [{'arc': {'radius': 20, 'angle': 45}}]}},
                {'floor_path': {'segments': [{'arc': {'radius': 20, 'angle': -45}}]}},
            )
        )
        end = layout.parts[-2].end
        self.assertEqual(end[2], (1, 0))
        self.assertAlmostEqual(end[0][0], 12.39 + 20 + 40 / math.sqrt(2))
        self.assertAlmostEqual(end[0][1], -40 * (1 - 1 / math.sqrt(2)))

    def test_diagonal_or_banked_grid_connection_is_rejected(self):
        for spec in (
            {'segments': [{'arc': {'radius': 20, 'angle': 45}}]},
            {'segments': [{'line': [20, 0, 0]}], 'bank': [[0, 0], [1, 10]]},
        ):
            with self.assertRaisesRegex(ValueError, 'grid connection'):
                Layout(route({'floor_path': spec}))

    def test_stops_are_inserted_at_exact_arc_length(self):
        piece = {'floor_path': {'segments': [{'line': [20, 0, 0]}], 'stops': [[0.37, 30]]}}
        layout = Layout(route(piece))
        stops = [p for p in layout.points if len(p) == 4]
        self.assertEqual(len(stops), 1)
        self.assertAlmostEqual(stops[0][0], 22.39 + 7.4)
        self.assertEqual(stops[0][2:], [None, 30])

    def test_mismatched_free_ports_and_folded_floor_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'mismatched width'):
            Layout(
                route(
                    {
                        'floor_path': {
                            'segments': [{'line': [10, 0, 0]}],
                            'width': [[0, 5], [1, 10]],
                        }
                    },
                    {'floor_path': {'segments': [{'line': [10, 0, 0]}]}},
                )
            )
        with self.assertRaisesRegex(ValueError, 'folds inside'):
            Layout(route({'floor_path': {'segments': [{'arc': {'radius': 1, 'angle': 360}}]}}))
        with self.assertRaisesRegex(ValueError, 'rail folds inside'):
            Layout(route({'rail_path': {'segments': [{'arc': {'radius': 1, 'angle': 360}}]}}))

    def test_curved_floor_reward_uses_arc_distance_and_rotated_offset(self):
        layout = Layout(
            route(
                {
                    'floor_path': {'segments': [{'arc': {'radius': 20, 'angle': 90}}]},
                    'elements': [{'kind': 'P_Extra_Point', 'at': [math.pi * 5, 1]}],
                }
            )
        )
        _, _, p = layout.elements[0]
        # Halfway round a clockwise quarter circle, one unit toward its centre.
        self.assertAlmostEqual(p[0], 22.39 + 19 / math.sqrt(2), delta=0.04)
        self.assertAlmostEqual(p[2], -20 + 19 / math.sqrt(2), delta=0.04)

    def test_built_meshes_have_finite_nonzero_triangles_and_physics_groups(self):
        authored = route(
            {
                'rail_path': {
                    'segments': [
                        {'bezier': [[10, 0, 0], [20, 15, -3], [30, 15, -3]]},
                        {'bezier': [[40, 15, -3], [50, 0, 0], [60, 0, 0]]},
                    ],
                    'bank': [[0, 0], [0.3, 8], [0.7, -8], [1, 0]],
                }
            },
            {
                'floor_path': {
                    'segments': [{'line': [30, 0, 0]}],
                    'width': [[0, 5], [0.5, 12], [1, 5]],
                }
            },
            {
                'floor_polygon': {
                    'outline': [[0, -5], [10, -5], [10, -12], [30, -12], [30, 5], [0, 5]],
                    'entry': {'edge': 5},
                    'exit': {'edge': 3, 'at': 12 / 17},
                }
            },
        )
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            self.assertEqual(build(authored, folder, None, 'Free Shapes'), 0)
            manifest = json.loads((folder / 'manifest.json').read_text())
            objects = manifest['scenes'][0]['objects']
            self.assertTrue(any('Phys_FloorRails' in o['groups'] for o in objects))
            for file in ('sector1.obj', 'rails1.obj'):
                for tri in read_obj(folder / file):
                    self.assertTrue(all(math.isfinite(v) for p in tri.points for v in p))
                    a, b, c = tri.points
                    ab, ac = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
                    area = sum(
                        (ab[(i + 1) % 3] * ac[(i + 2) % 3] - ab[(i + 2) % 3] * ac[(i + 1) % 3]) ** 2
                        for i in range(3)
                    )
                    self.assertGreater(area, 1e-12, file)

    def test_helix_can_return_above_its_entry_without_reusing_the_lower_grid(self):
        authored = route(
            {'rail_path': {'segments': [{'arc': {'radius': 20, 'angle': 360, 'rise': 12}}]}}
        )
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(build(authored, Path(tmp), None, 'Helix'), 0)
            vertices = [p for tri in read_obj(Path(tmp) / 'sector1.obj') for p in tri.points]
            self.assertTrue(any(p[1] > 10 for p in vertices))

    def test_polygon_path_cannot_cut_across_a_notch(self):
        spec = {
            'outline': [[0, -5], [30, -5], [30, 30], [20, 30], [20, 5], [0, 5]],
            'entry': {'edge': 5},
            'exit': {'edge': 2},
        }
        with self.assertRaisesRegex(ValueError, 'steering path'):
            Layout(route({'floor_polygon': spec}))
        spec['path'] = [[25, 0], [25, 25]]
        self.assertTrue(Layout(route({'floor_polygon': spec})).points)


class TemplateTests(unittest.TestCase):
    def test_typed_parameters_repetition_and_mirroring(self):
        source = route(
            {'use': 'bend', 'with': {'radius': 30}, 'repeat': 2, 'mirror': True},
            definitions={
                'bend': {
                    'parameters': {'radius': 20},
                    'pieces': [
                        {'rail_path': {'segments': [{'arc': {'radius': '${radius}', 'angle': 45}}]}}
                    ],
                }
            },
        )
        before = json.dumps(source)
        expanded = expand(source)[0]
        self.assertEqual(
            expanded[1]['rail_path']['segments'][0]['arc'], {'radius': 30, 'angle': -45}
        )
        self.assertEqual(expanded[1], expanded[2])
        self.assertEqual(json.dumps(source), before)
        self.assertEqual(mirror(mirror(expanded[1])), expanded[1])

    def test_recursive_templates_and_unknown_parameters_fail(self):
        with self.assertRaisesRegex(ValueError, 'recursive'):
            expand(route({'use': 'a'}, definitions={'a': {'pieces': [{'use': 'a'}]}}))
        with self.assertRaisesRegex(ValueError, 'unknown parameters'):
            expand(route({'use': 'a', 'with': {'typo': 2}}, definitions={'a': {'pieces': []}}))


if __name__ == '__main__':
    unittest.main()
