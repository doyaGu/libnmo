"""Opt-in traversal of free geometry using the real Ballance gameplay files.

Set NMO_TEST_PHYSICS=1, BALLANCE, BALLANCE_ELEMENTS and NMO_TEST_CLI.
The elements directory comes from ballance_level_elements.py.
"""

import os
import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo
from ballance.route.build import build


@unittest.skipUnless(
    os.environ.get('NMO_TEST_PHYSICS') == '1'
    and os.environ.get('BALLANCE')
    and os.environ.get('BALLANCE_ELEMENTS')
    and os.environ.get('NMO_TEST_CLI'),
    'needs opt-in physics, Ballance and extracted element meshes',
)
class FreeformPhysicsTests(unittest.TestCase):
    def test_connectors_cross_wide_platform_native_bridge_and_variable_gauge_rails(self):
        from ballance.verify.play import play

        course = {
            'start': {'heading': '+z'},
            'death_zones': {},
            'sectors': [
                [
                    {'road': 15},
                    {'connector': {'length': 15, 'to': {'width': 8, 'bank': 3}}},
                    {'connector': {'length': 15, 'to': {'bank': 0}}},
                    {
                        'floor_polygon': {
                            'outline': [[0, -8], [20, -8], [20, 8], [0, 8]],
                            'entry': {'edge': 3, 'width': 8},
                            'exit': {'edge': 1, 'width': 10},
                        }
                    },
                    {'connector': {'length': 15, 'to': {'width': 5}}},
                    {'module': 'P_Modul_25'},
                    {'road': 15},
                    {'rail': 10},
                    {'connector': {'length': 15, 'to': {'gauge': 4.2, 'bank': 2}}},
                    {'connector': {'length': 15, 'to': {'gauge': 3.8, 'bank': 0}}},
                    {'floor_path': {'segments': [{'line': [10, 0, 0]}]}},
                    {'road': 15},
                ]
            ],
        }
        with tempfile.TemporaryDirectory(prefix='ballance-connectors-') as tmp:
            folder = Path(tmp)
            self.assertEqual(
                build(course, folder, Path(os.environ['BALLANCE_ELEMENTS']), 'Connectors'), 0
            )
            result = play(
                folder, 5000, Nmo(os.environ['NMO_TEST_CLI']), Path(os.environ['BALLANCE'])
            )
            self.assertIsNotNone(result.finished, result.stalls)
            self.assertEqual(result.falls, [])

    def test_native_fan_tower_support_leaves_the_entry_clear(self):
        from ballance.verify.scenery import check
        from ballance.view.scene import load_project
        from ballance.route.layout import Layout

        course = {
            'sectors': [
                [
                    {'road': 10},
                    {'trafo': 'paper'},
                    {'road': 10},
                    {'rail': 15},
                    {'tower': True},
                    {'road': 15},
                    {'trafo': 'wood'},
                    {'road': 15},
                ]
            ]
        }
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            self.assertEqual(build(course, folder, Path(os.environ['BALLANCE_ELEMENTS']), 'Fan'), 0)
            scene = load_project(folder)
            support = next(obj for obj in scene if '_FanTower_Pillar_' in obj.name)
            self.assertTrue(support.triangles)
            self.assertEqual(support.groups, [])
            self.assertEqual(check(scene, Layout(course)), [])

    def test_pressure_route_requires_paper_for_the_fan(self):
        from ballance.verify.play import play

        for ball in ('paper', 'wood'):
            course = {
                'start': {'heading': '+x'},
                'death_zones': {},
                'sectors': [
                    [
                        {'road': 10},
                        {'trafo': ball},
                        {'road': 10},
                        {'module': 'P_Modul_41', 'count': 4},
                        {'road': 10},
                        {'rail': 10},
                        {'fan': 14},
                        {'road': 15},
                    ],
                    [{'road': 10}, {'trafo': 'wood'}, {'road': 15}],
                ],
            }
            with (
                self.subTest(ball=ball),
                tempfile.TemporaryDirectory(prefix='ballance-paper-gate-') as tmp,
            ):
                folder = Path(tmp)
                self.assertEqual(
                    build(course, folder, Path(os.environ['BALLANCE_ELEMENTS']), 'Paper Gate'), 0
                )
                result = play(
                    folder, 4000, Nmo(os.environ['NMO_TEST_CLI']), Path(os.environ['BALLANCE'])
                )
                if ball == 'paper':
                    self.assertIsNotNone(result.finished, result.stalls)
                    self.assertEqual(result.falls, [])
                else:
                    self.assertIsNone(result.finished)
                    self.assertFalse(
                        any(event == 'Checkpoint reached' for _, event in result.events)
                    )

    def test_curved_single_beam_and_standard_rail_transitions(self):
        from ballance.verify.play import play

        course = {
            'start': {'heading': '+x'},
            'death_zones': {},
            'sectors': [
                [
                    {'road': 15},
                    {'rail': 10},
                    {'beam_path': {'segments': [{'wave': {'length': 80, 'amplitude': 5}}]}},
                    {'rail': 10},
                    {'road': 15},
                ]
            ],
        }
        with tempfile.TemporaryDirectory(prefix='ballance-beam-physics-') as tmp:
            folder = Path(tmp)
            self.assertEqual(
                build(course, folder, Path(os.environ['BALLANCE_ELEMENTS']), 'Curved Beam'), 0
            )
            result = play(
                folder, 3000, Nmo(os.environ['NMO_TEST_CLI']), Path(os.environ['BALLANCE'])
            )
            self.assertIsNotNone(result.finished, result.stalls)
            self.assertEqual(result.falls, [])

    def test_authored_death_zones_catch_a_failed_jump_promptly(self):
        from ballance.verify.play import play, STEER_FROM

        course = {'start': {'heading': '+x'}, 'death_zones': {}, 'sectors': [[{'road': 60}]]}
        with tempfile.TemporaryDirectory(prefix='ballance-death-physics-') as tmp:
            folder = Path(tmp)
            self.assertEqual(
                build(course, folder, Path(os.environ['BALLANCE_ELEMENTS']), 'Fall Probe'), 0
            )
            # Leave the middle of a real floor at right angles to its edge.
            (folder / 'route_points.json').write_text(json.dumps([[0, 0], [30, 0], [30, 80]]))
            result = play(
                folder, 1600, Nmo(os.environ['NMO_TEST_CLI']), Path(os.environ['BALLANCE'])
            )
            self.assertTrue(result.falls)
            self.assertLess(result.falls[0] - STEER_FROM, 600)
            with (folder / 'steer.csv').open() as stream:
                trace = {int(row['frame']): row for row in csv.DictReader(stream)}
            height = float(trace[result.falls[0]]['y'])
            self.assertLess(height, -12)
            self.assertGreater(height, -45)

    def test_helix_banked_floor_and_concave_platform(self):
        from ballance.verify.geometry import check
        from ballance.verify.play import play
        from ballance.view.scene import load

        course = {
            'death_zones': {},
            'start': {'heading': '+z'},
            'definitions': {
                'bend': {
                    'parameters': {'angle': 90},
                    'pieces': [
                        {
                            'floor_path': {
                                'segments': [{'arc': {'radius': 25, 'angle': '${angle}'}}],
                                'width': [[0, 5], [0.5, 8], [1, 5]],
                                'bank': [[0, 0], [0.5, 3], [1, 0]],
                            }
                        }
                    ],
                }
            },
            'sectors': [
                [
                    {'road': 15},
                    {
                        'rail_path': {
                            'segments': [{'arc': {'radius': 20, 'angle': 360, 'rise': 12}}]
                        }
                    },
                    {'road': 15},
                ],
                [
                    {'road': 10},
                    {'use': 'bend'},
                    {'use': 'bend', 'mirror': True},
                    {
                        'floor_polygon': {
                            'outline': [[0, -5], [10, -5], [10, -12], [30, -12], [30, 5], [0, 5]],
                            'entry': {'edge': 5},
                            'exit': {'edge': 3, 'at': 12 / 17},
                        },
                        'elements': [{'kind': 'P_Extra_Point', 'at': [20, 0]}],
                    },
                    {'road': 20},
                ],
            ],
        }
        nmo = Nmo(os.environ['NMO_TEST_CLI'])
        with tempfile.TemporaryDirectory(prefix='ballance-freeform-physics-') as tmp:
            folder = Path(tmp)
            self.assertEqual(
                build(course, folder, Path(os.environ['BALLANCE_ELEMENTS']), 'Freeform Physics'), 0
            )
            self.assertEqual(check(load(folder, nmo, hidden=True)), [])
            result = play(folder, 5000, nmo, Path(os.environ['BALLANCE']))
            self.assertIsNotNone(result.finished, result.stalls)
            self.assertEqual(result.falls, [])
            self.assertEqual(result.missed, [])
            self.assertEqual(sum(what == 'Checkpoint reached' for _, what in result.events), 1)


if __name__ == '__main__':
    unittest.main()
