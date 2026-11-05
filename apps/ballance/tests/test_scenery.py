"""Scenery must clear lower routes, not just the floor under its top."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.obj import write_box, read_obj
from ballance.kit.rails import Ring
from ballance.route import scenery
from ballance.route.assets import Assets
from ballance.route.layout import Layout, Part


def returning_rail(rise):
    return Layout(
        {
            'start': {'heading': '+x'},
            'sectors': [
                [
                    {'road': 10},
                    {'rail': 10},
                    {
                        'rail_path': {
                            'segments': [{'arc': {'radius': 6.5, 'angle': 180, 'rise': rise}}]
                        }
                    },
                    {'rail': 30},
                    {'rail_path': {'segments': [{'arc': {'radius': 12, 'angle': -90}}]}},
                    {'road': 15},
                ]
            ],
        }
    )


def scenery_assets(folder):
    assets = Assets(folder, [], [], None)
    for kind, box in (
        ('Tower', (-3.21, 3.21, 0, 17.68, -3.56, 3.56)),
        ('Pillar', (-2.34, 2.34, -328, 0, -2.34, 2.34)),
        ('Stick', (-1.13, 1.13, -251, 0, -1.13, 1.13)),
    ):
        assets.meshes[kind] = kind + '.obj'
        write_box(folder / assets.mesh(kind), box)
    return assets


class SceneryTests(unittest.TestCase):
    def test_orphan_module_column_cannot_get_an_automatic_side_brace(self):
        layout = Layout({'start': {'heading': '+x'}, 'sectors': [[{'road': 30}]]})
        layout.balloon = ([0, -40, 0], (1, 0))
        layout.supports = [('FanTower_Pillar', 1, [40, 50, 0], 90)]
        original = list(layout.supports[0][2])
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            assets = scenery_assets(folder)
            assets.meshes['FanTower_Pillar'] = 'FanTower_Pillar.obj'
            write_box(folder / 'FanTower_Pillar.obj', (-3.34, -0.62, -325.2, -1.62, 1.14, 3.86))
            with self.assertRaisesRegex(ValueError, 'no owning native module'):
                scenery.fit(layout, assets, [], [[0, -36.3, 24], [0, -36.3, -24]])
            self.assertEqual(layout.supports[0][2], original)
            self.assertFalse(assets.has('Scenery_Braces'))

    def test_platform_column_clears_the_ball_overhanging_a_rail_end(self):
        layout = Layout({'start': {'heading': '+x'}, 'sectors': [[{'road': 30}]]})
        layout.pillars = [[15, -5, 0]]
        layout.parts.append(
            Part(
                len(layout.parts),
                1,
                'rail',
                segments=[(Ring(0, -10, 0, 1, 0), Ring(11.5, -10, 0, 1, 0))],
            )
        )
        with tempfile.TemporaryDirectory() as tmp:
            _, pillars, _, changes = scenery.fit(layout, scenery_assets(Path(tmp)), [], [])
        self.assertEqual(len(pillars), 1)
        self.assertGreater(pillars[0][0], 15)
        self.assertEqual(changes[0]['action'], 'moved')

    def test_crossing_rails_keep_the_platform_supported_by_a_clear_brace(self):
        layout = Layout(
            {
                'start': {'heading': '+x'},
                'sectors': [[{'road': 10}, {'area': [15, 15]}, {'road': 10}]],
            }
        )
        layout.pillars = [[29.89, -5, 0]]
        layout.parts.append(
            Part(
                len(layout.parts),
                1,
                'rail',
                segments=[(Ring(15, -14, z, 1, 0), Ring(45, -14, z, 1, 0)) for z in (-4, 0, 4)],
            )
        )
        with tempfile.TemporaryDirectory() as tmp:
            assets = scenery_assets(Path(tmp))
            with self.assertRaisesRegex(ValueError, 'explicitly allow'):
                scenery.fit(layout, assets, [], [])
            _, pillars, _, changes = scenery.fit(
                layout,
                assets,
                [],
                [],
                {'route/position/sector/0/1/pillar/0': {'allow_brace': True}},
            )
            self.assertEqual(len(pillars), 1)
            self.assertEqual(changes[0]['action'], 'braced')
            self.assertTrue(assets.has('Scenery_Braces'))
            triangles = read_obj(Path(tmp) / assets.mesh('Scenery_Braces'))
            self.assertTrue(triangles)
            # The bracket remains above the ball's top on the lower rails.
            self.assertGreater(min(p[1] for t in triangles for p in t.points), -10.4)
            self.assertGreater(abs(pillars[0][2]), 6.25 + 2.34)

    def test_lamp_and_its_whole_column_move_off_the_lower_rail(self):
        layout = returning_rail(-12)
        lamps = scenery.towers(layout)
        crossing = [17.4, -3.68, -13.0]
        self.assertTrue(
            any(abs(p[0] - crossing[0]) < 1e-6 and abs(p[2] - crossing[2]) < 1e-6 for p in lamps)
        )
        with tempfile.TemporaryDirectory() as tmp:
            fitted, _, _, changes = scenery.fit(layout, scenery_assets(Path(tmp)), lamps, [])
        self.assertFalse(
            any(abs(p[0] - crossing[0]) < 1e-6 and abs(p[2] - crossing[2]) < 1e-6 for p in fitted)
        )
        self.assertEqual(len(fitted), len(lamps))
        self.assertTrue(changes)

    def test_a_rail_above_the_roof_does_not_move_the_lamp(self):
        layout = returning_rail(80)
        lamps = scenery.towers(layout)
        with tempfile.TemporaryDirectory() as tmp:
            fitted, _, _, _ = scenery.fit(layout, scenery_assets(Path(tmp)), lamps, [])
        self.assertTrue(any(abs(p[0] - 17.4) < 1e-6 and abs(p[2] + 13) < 1e-6 for p in fitted))


if __name__ == '__main__':
    unittest.main()
