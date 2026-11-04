"""Regression checks for reusable route operations."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.autoplay.steer import format_points, parse

ROOT = Path(__file__).resolve().parents[1]


class RouteInputTests(unittest.TestCase):
    def test_precision_points_survive_route_editing(self):
        route = parse('0,0;10,2,,precision;20,0,,rush;30,0,,60')
        self.assertTrue(route[1].precision)
        self.assertEqual(format_points(route), '0,0;10,2,,precision;20,0,,rush;30,0,,60')


if __name__ == '__main__':
    unittest.main()
