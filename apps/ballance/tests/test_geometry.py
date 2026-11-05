"""Tests for level geometry checks."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.obj import Triangle
from ballance.verify.geometry import holes, open_walls
from ballance.view.scene import SceneObject


class BallanceTests(unittest.TestCase):
    def test_wall_end_covered_by_another_wall_face(self):
        def obj(name, points):
            return SceneObject(
                name, ['Phys_Floors'], [Triangle(None, points, None, None)], (0, 0, 0)
            )

        end = obj('road', [(0, 0, 0), (0, 5, 0), (1, 0, 0)])
        covering = obj('wall', [(-1, 0, 0), (1, 0, 0), (0, 10, 0)])
        partial = obj('wall', [(-1, 0, 0), (1, 0, 0), (0, 2, 0)])
        lower = obj('wall', [(-1, 0, 0), (1, 0, 0), (0, 3, 0)])
        upper = obj('wall', [(-1, 2, 0), (1, 2, 0), (0, 5, 0)])
        distant = obj('wall', [(-1, 0, 0.06), (1, 0, 0.06), (0, 10, 0.06)])
        partial_edge = obj('wall', [(0, 0, 0), (0, 3, 0), (-1, 0, 0)])

        def road_findings(scene):
            return [f for f in open_walls(scene) if f.what == 'road']

        self.assertEqual(len(road_findings([end])), 1)
        self.assertEqual(road_findings([end, covering]), [])
        self.assertEqual(road_findings([end, lower, upper]), [])
        self.assertEqual(len(road_findings([end, partial, partial])), 1)
        self.assertEqual(len(road_findings([end, distant])), 1)
        self.assertEqual(len(road_findings([end, partial_edge, partial_edge])), 1)

    def test_slanted_wall_join_from_level_13(self):
        source = Triangle(
            None, [(0, 0, 0), (5.06905, 3.81244, 0), (0.02802, 1.60721, 0)], None, None
        )
        adjacent = Triangle(
            None, [(0.0872, 4.9989, 0), (-4.9817, 1.1864, 0), (0, 0, 0)], None, None
        )
        floor = SceneObject('road', ['Phys_Floors'], [source, adjacent], (0, 0, 0))
        self.assertTrue(open_walls([SceneObject('road', ['Phys_Floors'], [source], (0, 0, 0))]))
        self.assertEqual(open_walls([floor]), [])

    def test_fan_tower_wall_meets_road_but_lantern_tower_is_excluded(self):
        edge = [(0, 0, 0), (0, 5, 0)]

        def wall(name, third):
            return SceneObject(
                name, ['Phys_Floors'], [Triangle(None, edge + [third], None, None)], (0, 0, 0)
            )

        road = wall('A03_Floor_02', (-1, 0, 0))
        fan = wall('A03_Floor_FanTower_01', (0, 0, 1))
        self.assertEqual(len(open_walls([road])), 1)
        self.assertEqual(open_walls([road, fan]), [])
        for name in (
            'Tower_01',
            'A01_Tower',
            'A01_Tower_01',
            'A05_Tower01',
            'A01_Tower1',
            'A05_Floor_Columns_Top01',
        ):
            with self.subTest(name=name):
                self.assertEqual(len(open_walls([road, wall(name, (0, 0, 1))])), 1)

    def test_column_top_opening_is_not_a_floor_hole(self):
        triangles = []
        for x0, x1, z0, z1 in ((0, 1, 0, 4), (3, 4, 0, 4), (1, 3, 0, 1), (1, 3, 3, 4)):
            a, b, c, d = (x0, 0, z0), (x0, 0, z1), (x1, 0, z1), (x1, 0, z0)
            triangles.extend(
                [Triangle(None, [a, b, c], None, None), Triangle(None, [a, c, d], None, None)]
            )
        floor = SceneObject('A05_Floor_01', ['Phys_Floors'], triangles, (0, 0, 0))
        column = SceneObject('A05_Floor_Columns_Top01', ['Phys_Floors'], triangles, (0, 0, 0))
        self.assertTrue(holes([floor]))
        self.assertEqual(holes([column]), [])


if __name__ == '__main__':
    unittest.main()
