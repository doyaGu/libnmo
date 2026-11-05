"""Connection contracts, generated transitions and material continuity."""

from dataclasses import replace
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.obj import read_obj
from ballance.route.build import build
from ballance.route.identity import Origin
from ballance.route.layout import Layout
from ballance.route.ports import Port, Section, ConnectionError, connect
from ballance.kit.shapes import V_SECTION, FLAT_SECTION, inside


class PortTests(unittest.TestCase):
    def test_joint_reports_both_sources_and_rejects_pose_and_section_mismatches(self):
        before = Port(
            'first',
            'exit',
            (0, 0, 0),
            (1, 0),
            Section('floor'),
            Origin('first', '/sectors/0/0'),
            distance=13,
        )
        after = replace(
            before, owner='second', name='entry', source=Origin('second', '/sectors/0/1')
        )
        for changed, rule in [
            (replace(after, position=(0.01, 0, 0)), 'position gap'),
            (replace(after, heading=(0, 1)), 'heading mismatch'),
            (replace(after, section=Section('floor', width=6)), 'mismatched width'),
            (replace(after, section=Section('floor', bank=2)), 'mismatched bank'),
            (replace(after, section=Section('floor', depth=10)), 'mismatched depth'),
            (
                replace(after, section=Section('floor', profile=tuple(FLAT_SECTION))),
                'mismatched section',
            ),
        ]:
            with self.subTest(rule=rule), self.assertRaises(ConnectionError) as error:
                connect(before, changed)
            self.assertEqual(error.exception.rule, rule)
            self.assertIn('/sectors/0/0', str(error.exception))
            self.assertIn('/sectors/0/1', str(error.exception))
        self.assertEqual(connect(before, after).kind, 'floor-seam')
        rail = replace(after, section=Section('rail'))
        self.assertEqual(connect(before, rail).kind, 'rail-floor-dock')
        with self.assertRaisesRegex(ConnectionError, 'standard ports'):
            connect(before, replace(rail, section=Section('rail', gauge=4.2)))

    def test_equivalent_profiles_with_different_knots_share_a_port(self):
        profile = (
            (-0.5, V_SECTION[0][1]),
            (-0.25, V_SECTION[0][1] / 2),
            (0, 0),
            (0.25, V_SECTION[0][1] / 2),
            (0.5, V_SECTION[0][1]),
        )
        a = Port('a', 'exit', (1, 2, 3), (0, -1), Section('floor'))
        b = replace(a, owner='b', name='entry', section=Section('floor', profile=profile))
        self.assertEqual(connect(a, b, grid=True).kind, 'floor-seam')
        self.assertEqual(b.data()['up'], (0, 1, 0))

    def test_connector_inherits_start_and_restores_native_module_interface(self):
        layout = Layout(
            {
                'schema_version': 1,
                'sectors': [
                    [
                        {'id': 'entry', 'road': 10},
                        {'id': 'widen', 'connector': {'length': 10, 'to': {'width': 9, 'bank': 4}}},
                        {
                            'id': 'restore',
                            'connector': {'length': 15, 'to': {'width': 5, 'bank': 0}},
                        },
                        {'id': 'bridge', 'module': 'P_Modul_25'},
                        {'id': 'exit', 'road': 10},
                    ]
                ],
            }
        )
        transitions = layout.connection_report()['transitions']
        self.assertEqual(len(transitions), 2)
        self.assertEqual(transitions[1]['path']['width'], [[0, 9], [1, 5]])
        self.assertEqual(transitions[1]['path']['bank'], [[0, 4], [1, 0]])
        bridge = next(c for c in layout.connections if c.after.owner == 'route/id/bridge')
        self.assertTrue(bridge.before.section.standard)
        self.assertEqual(bridge.before.owner, 'route/id/restore')
        self.assertEqual(bridge.data()['uv'], 'native-grid')

    def test_custom_section_connector_resamples_profiles_without_changing_entry_shape(self):
        course = {
            'sectors': [
                [
                    {'road': 10},
                    {
                        'connector': {
                            'length': 10,
                            'to': {'section': [[-0.5, 1], [-0.2, 0.2], [0.2, 0.2], [0.5, 1]]},
                        }
                    },
                    {'connector': {'length': 10, 'to': {'section': 'v'}}},
                    {'road': 10},
                ]
            ]
        }
        layout = Layout(course)
        self.assertEqual(len(layout.transitions), 2)
        self.assertEqual(
            [p[0] for p in layout.transitions[0]['path']['section']], [-0.5, -0.2, 0, 0.2, 0.5]
        )

    def test_connectors_and_platforms_rotate_and_mirror_together(self):
        template = {
            'pieces': [
                {
                    'connector': {
                        'segments': [{'arc': {'radius': 30, 'angle': 45}}],
                        'to': {'width': 8, 'bank': 3},
                    }
                },
                {
                    'connector': {
                        'segments': [{'arc': {'radius': 30, 'angle': -45}}],
                        'to': {'bank': 0},
                    }
                },
                {
                    'floor_polygon': {
                        'outline': [[0, -8], [20, -8], [20, 8], [0, 8]],
                        'entry': {'edge': 3, 'width': 8},
                        'exit': {'edge': 1, 'width': 10},
                    }
                },
                {'connector': {'length': 10, 'to': {'width': 5}}},
            ]
        }
        layouts = []
        for heading in ('+x', '+z'):
            for mirror in (False, True):
                layout = Layout(
                    {
                        'start': {'heading': heading},
                        'definitions': {'shape': template},
                        'sectors': [
                            [{'road': 10}, {'use': 'shape', 'mirror': mirror}, {'road': 10}]
                        ],
                    }
                )
                layouts.append(layout)
                self.assertEqual(
                    layout.transitions[0]['to']['section']['bank'], -3 if mirror else 3
                )
                self.assertTrue(
                    all(
                        math.dist(c.before.position, c.after.position) < 1e-9
                        for c in layout.connections
                    )
                )
        # Reflections/rotations preserve all generated mouth widths.
        self.assertEqual(
            [[c.before.section.width for c in layout.connections] for layout in layouts],
            [[c.before.section.width for c in layouts[0].connections]] * 4,
        )

    def test_floor_uv_phase_continues_across_independently_authored_paths(self):
        course = {
            'start': {'heading': '+x'},
            'sectors': [
                [
                    {'road': 10},
                    {'floor_path': {'segments': [{'line': [7, 0, 0]}]}},
                    {'connector': {'length': 8, 'to': {'width': 5}}},
                    {'road': 10},
                ]
            ],
        }
        layout = Layout(course)
        joint = next(c for c in layout.connections if c.before.distance == 7)
        self.assertEqual(joint.after.distance, 7)
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            build(course, folder, None, 'UV')
            top = [t for t in read_obj(folder / 'sector1.obj') if t.material == 'Floor_Top_Profile']
            seam = [
                (p, uv) for t in top for p, uv in zip(t.points, t.uvs) if abs(p[0] - 29.39) < 1e-5
            ]
            self.assertTrue(seam)
            self.assertEqual({round(uv[1], 6) for _, uv in seam}, {1.4})

    def test_wide_platform_mouth_keeps_brick_bands_and_has_no_cap(self):
        course = {
            'start': {'heading': '+x'},
            'sectors': [
                [
                    {'road': 10},
                    {'connector': {'length': 10, 'to': {'width': 8}}},
                    {
                        'floor_polygon': {
                            'outline': [[0, -8], [20, -8], [20, 8], [0, 8]],
                            'entry': {'edge': 3, 'width': 8},
                            'exit': {'edge': 1, 'width': 10},
                        }
                    },
                    {'connector': {'length': 10, 'to': {'width': 5}}},
                    {'road': 10},
                ]
            ],
        }
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            build(course, folder, None, 'Wide mouth')
            triangles = read_obj(folder / 'sector1.obj')
            adapters = [
                t
                for t in triangles
                if t.material == 'Floor_Top_ProfileFlat' and all(32 < p[0] < 63 for p in t.points)
            ]
            self.assertTrue(adapters)
            for t in adapters:
                self.assertLessEqual(
                    max(p[2] for p in t.points) - min(p[2] for p in t.points), 2.50001
                )
            borders = [t for t in triangles if t.material == 'Floor_Top_Border']
            for x, halfwidth in ((37.49, 4), (57.29, 5)):
                for sign in (-1, 1):
                    self.assertTrue(
                        any(
                            inside((x, sign * (halfwidth - 0.2)), [(p[0], p[2]) for p in t.points])
                            for t in borders
                        )
                    )
            for x in (32.39, 37.39, 57.39, 62.39):
                caps = [
                    t
                    for t in triangles
                    if t.material == 'Floor_Side'
                    and all(abs(p[0] - x) < 1e-5 for p in t.points)
                    and any(abs(p[2]) < 3.9 for p in t.points)
                ]
                self.assertEqual(caps, [])

    def test_unsupported_connector_and_overwide_polygon_ports_fail_explicitly(self):
        for spec, message in [
            ({'length': 0, 'to': {}}, 'positive'),
            ({'length': 10, 'to': {'kind': 'rail'}}, 'one cross section'),
            ({'length': 10, 'to': {'depth': 20}}, 'depth'),
            ({'length': 10, 'to': {'typo': 3}}, 'target accepts'),
        ]:
            with self.subTest(spec=spec), self.assertRaisesRegex(ValueError, message):
                Layout({'sectors': [[{'road': 10}, {'connector': spec}, {'road': 10}]]})
        with self.assertRaisesRegex(ValueError, 'fit its edge'):
            Layout(
                {
                    'sectors': [
                        [
                            {'road': 10},
                            {
                                'floor_polygon': {
                                    'outline': [[0, -5], [20, -5], [20, 5], [0, 5]],
                                    'entry': {'edge': 3, 'width': 12},
                                    'exit': {'edge': 1},
                                }
                            },
                        ]
                    ]
                }
            )


if __name__ == '__main__':
    unittest.main()
