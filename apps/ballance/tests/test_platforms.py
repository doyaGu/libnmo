"""Polygon holes and named multiway ports through the build interface."""

import math
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ballance.core.obj import read_obj
from ballance.kit.shapes import inside, cross
from ballance.kit.polygons import tessellate
from ballance.route.build import build
from ballance.route.layout import Layout


def normal(triangle):
    a, b, c = triangle.points
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    n = [u[(i + 1) % 3] * v[(i + 2) % 3] - u[(i + 2) % 3] * v[(i + 1) % 3] for i in range(3)]
    length = math.sqrt(sum(x * x for x in n))
    return [x / length for x in n]


def platform():
    return {
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
        'id': 'platform',
    }


def course(piece=None):
    return {
        'schema_version': 1,
        'start': {'heading': '+x'},
        'sectors': [
            [
                {'id': 'entry', 'road': 10},
                piece or platform(),
                {'id': 'restore', 'trafo': 'wood'},
                {'id': 'exit', 'road': 10},
            ]
        ],
    }


def merge_course():
    def hub(id, name=None):
        p = {
            'id': id,
            'floor_polygon': {
                'outline': [[0, -10], [20, -10], [20, 10], [0, 10]],
                'entry': {'edge': 3},
                'exit': {'edge': 1},
                'ports': {'side': {'edge': 0, 'path': [[10, 0]]}},
            },
        }
        if name:
            p['name'] = name
        return p

    first = hub('first')
    second = hub('second', 'destination')
    first['branches'] = [
        {
            'id': 'detour',
            'port': 'side',
            'to': 'destination',
            'to_port': 'side',
            'take': True,
            'route': [
                {
                    'id': 'curve',
                    'rail_path': {'segments': [{'bezier': [[20, 0, 0], [20, 50, 0], [0, 50, 0]]}]},
                }
            ],
        }
    ]
    r = {
        'schema_version': 1,
        'start': {'heading': '+x'},
        'sectors': [
            [
                {'id': 'entry', 'road': 10},
                first,
                {'id': 'middle', 'road': 20},
                second,
                {'id': 'exit', 'road': 60},
            ]
        ],
    }
    return r


class PlatformTests(unittest.TestCase):
    def test_void_tessellation_preserves_area_for_concavity_multiple_holes_and_winding(self):
        outline = [(0, 0), (40, 0), (40, 30), (20, 30), (20, 20), (0, 20)]
        holes = [[(5, 5), (10, 5), (10, 10), (5, 10)], [(25, 5), (35, 5), (35, 15), (25, 15)]]
        for reverse in (False, True):
            outer = outline[::-1] if reverse else outline
            voids = [h[::-1] for h in holes] if reverse else holes
            triangles = tessellate(outer, voids)
            self.assertAlmostEqual(sum(abs(cross(*t)) / 2 for t in triangles), 875)
            for tri in triangles:
                middle = tuple(sum(p[k] for p in tri) / 3 for k in (0, 1))
                self.assertTrue(inside(middle, outline))
                self.assertFalse(any(inside(middle, h) for h in holes))

    def test_export_keeps_top_bottom_and_bricks_out_of_hole_and_builds_inward_walls(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            build(course(), out, None, 'Void')
            triangles = read_obj(out / 'sector1.obj')
            hole = Layout(course()).platforms[0]['holes'][0]
            centre = tuple(sum(p[k] for p in hole) / 4 for k in (0, 2))
            self.assertFalse(
                any(
                    abs(normal(t)[1]) > 0.9 and inside(centre, [(p[0], p[2]) for p in t.points])
                    for t in triangles
                )
            )
            for a, b in zip(hole, hole[1:] + hole[:1]):
                middle = tuple((a[k] + b[k]) / 2 for k in range(3))
                walls = [
                    t
                    for t in triangles
                    if t.material == 'Floor_Side'
                    and abs(normal(t)[1]) < 0.1
                    and all(
                        abs(cross((a[0], a[2]), (b[0], b[2]), (p[0], p[2]))) < 1e-6
                        for p in t.points
                    )
                ]
                self.assertTrue(walls)
                direction = (centre[0] - middle[0], centre[1] - middle[2])
                self.assertTrue(
                    all(
                        normal(t)[0] * direction[0] + normal(t)[2] * direction[1] > 0 for t in walls
                    )
                )
                outward = (
                    middle[0] + 0.1 * (middle[0] - centre[0]),
                    middle[2] + 0.1 * (middle[2] - centre[1]),
                )
                self.assertTrue(
                    any(
                        t.material == 'Floor_Top_Border'
                        and inside(outward, [(p[0], p[2]) for p in t.points])
                        for t in triangles
                    )
                )

    def test_rotated_mirrored_branch_uses_named_port_and_keeps_identity(self):
        for heading in ('+x', '+z'):
            for mirrored in (False, True):
                r = {
                    'schema_version': 1,
                    'start': {'heading': heading},
                    'definitions': {'hub': {'pieces': [platform()]}},
                    'sectors': [
                        [
                            {'id': 'entry', 'road': 10},
                            {'id': 'hub', 'use': 'hub', 'mirror': mirrored},
                            {'id': 'exit', 'road': 10},
                        ]
                    ],
                }
                layout = Layout(r)
                joint = next(c for c in layout.connections if c.before.name == 'paper')
                self.assertIn('hub', joint.before.owner)
                self.assertIn('/branches/id/spur/', joint.after.owner)
                self.assertLess(math.dist(joint.before.position, joint.after.position), 1e-8)
                self.assertEqual(len(layout.platforms[0]['holes']), 1)

    def test_four_way_hub_visits_multiple_ports_in_order_and_checks_width(self):
        p = platform()
        p['floor_polygon']['holes'] = []
        p['floor_polygon']['ports']['return'] = {'edge': 2, 'width': 8, 'path': [[20, 0]]}
        p['branches'].append(
            {
                'id': 'second',
                'port': 'return',
                'visit': True,
                'route': [
                    {'id': 'narrow', 'connector': {'length': 10, 'to': {'width': 5}}},
                    {'id': 'wood', 'trafo': 'wood'},
                    {'id': 'end', 'road': 5},
                ],
            }
        )
        layout = Layout(course(p))
        self.assertEqual(len(layout.platforms[0]['ports']), 4)
        joint = next(c for c in layout.connections if c.before.name == 'return')
        self.assertEqual(joint.before.section.width, 8)
        ends = [
            part.end[0]
            for part in layout.parts
            if part.origin and part.origin.entity_id.endswith(('/id/trafo', '/id/wood'))
        ]
        visits = [
            min(range(len(layout.points)), key=lambda i: math.dist(layout.points[i][:2], end))
            for end in ends
        ]
        self.assertEqual(visits, sorted(visits))
        self.assertEqual(layout.last_ball, 'Wood')
        p['branches'][1]['route'].pop(0)
        with self.assertRaisesRegex(ValueError, 'standard|width'):
            Layout(course(p))

    def test_invalid_voids_paths_elements_and_unused_ports_are_rejected(self):
        for hole in (
            [[[0, -5], [4, -5], [4, 5], [0, 5]]],
            [[[10, 4], [30, 4], [30, 12], [10, 12]], [[15, 5], [20, 5], [20, 8], [15, 8]]],
        ):
            p = platform()
            p['floor_polygon']['holes'] = hole
            with self.assertRaisesRegex(ValueError, 'hole'):
                Layout(course(p))
        p = platform()
        p['floor_polygon']['path'] = [[20, 8]]
        with self.assertRaisesRegex(ValueError, 'crosses a hole'):
            Layout(course(p))
        p = platform()
        p['elements'] = [{'kind': 'P_Extra_Point', 'at': [20, 8]}]
        with self.assertRaisesRegex(ValueError, 'inside a hole'):
            Layout(course(p))
        p = platform()
        p['branches'] = []
        p['name'] = 'unconnected'
        with self.assertRaisesRegex(ValueError, 'unconnected ports'):
            Layout(course(p))

    def test_side_route_can_merge_into_a_named_platform_port(self):
        r = merge_course()
        layout = Layout(r)
        joined = next(
            c
            for c in layout.connections
            if c.after.owner == 'route/id/second' and c.after.name == 'side'
        )
        self.assertIn('detour', joined.before.owner)
        self.assertEqual(joined.kind, 'rail-floor-dock')
        self.assertTrue(layout.polygon_nodes['destination']['side']['used'])
        r['sectors'][0][1]['branches'][0]['route'][0]['rail_path']['segments'][0]['bezier'][-1][
            1
        ] = 49
        with self.assertRaisesRegex(ValueError, 'position gap'):
            Layout(r)

    def test_support_regions_cover_a_capital_together_but_never_bridge_a_gap(self):
        from ballance.route.attachments import supported

        box = (0, 4, -10, 0, 0, 4)
        self.assertTrue(supported(box, [(0, 2, 0, 5, 0, 4), (2, 4, 0, 5, 0, 4)]))
        self.assertFalse(supported(box, [(0, 1, 0, 5, 0, 4), (3, 4, 0, 5, 0, 4)]))
        self.assertFalse(supported(box, [(0, 2, 0, 5, 0, 4), (2, 4, 2, 7, 0, 4)]))

    @unittest.skipUnless(
        os.environ.get('NMO_TEST_PHYSICS') == '1'
        and os.environ.get('BALLANCE_ELEMENTS')
        and os.environ.get('BALLANCE')
        and os.environ.get('NMO_TEST_CLI'),
        'requires native physics resources',
    )
    def test_native_ball_visits_side_branch_and_returns_across_a_perforated_platform(self):
        from ballance.core.nmo import Nmo
        from ballance.verify.play import play
        from ballance.route.gameplay import compile_contracts
        from ballance.verify.gameplay import inspect

        r = course()
        r['gameplay'] = {
            'contracts': [
                {
                    'id': 'paper',
                    'at': 'route/id/platform/branches/id/spur/id/trafo',
                    'ball': 'Paper',
                },
                {'id': 'returned', 'at': 'route/id/platform', 'ball': 'Paper', 'after': ['paper']},
                {'id': 'finish', 'event': 'Level_Finish', 'sector': 1, 'after': ['returned']},
            ]
        }
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            build(r, out, Path(os.environ['BALLANCE_ELEMENTS']), 'Perforated hub')
            result = play(out, 5000, Nmo(os.environ['NMO_TEST_CLI']), Path(os.environ['BALLANCE']))
            self.assertIsNotNone(result.finished, result.stalls)
            self.assertEqual(result.falls, [])
            self.assertEqual(
                inspect(out, compile_contracts(r, Layout(r)), finish=result.finished)['status'],
                'passed',
            )

    @unittest.skipUnless(
        os.environ.get('NMO_TEST_PHYSICS') == '1'
        and os.environ.get('BALLANCE_ELEMENTS')
        and os.environ.get('BALLANCE')
        and os.environ.get('NMO_TEST_CLI'),
        'requires native physics resources',
    )
    def test_native_ball_takes_rail_branch_and_merges_back_through_target_port(self):
        from ballance.core.nmo import Nmo
        from ballance.verify.play import play

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            build(merge_course(), out, Path(os.environ['BALLANCE_ELEMENTS']), 'Rail merge')
            result = play(out, 5000, Nmo(os.environ['NMO_TEST_CLI']), Path(os.environ['BALLANCE']))
            self.assertIsNotNone(result.finished, result.stalls)
            self.assertEqual(result.falls, [])
            self.assertGreater(max(p[2] for _, p in result.trace), 20)


if __name__ == '__main__':
    unittest.main()
