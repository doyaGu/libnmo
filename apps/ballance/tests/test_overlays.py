"""Tests for map route overlays."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))



class BallanceTests(unittest.TestCase):
    def test_route_overlays_describe_all_point_actions(self):
        try:
            from ballance.view.overlays import Text, route_overlays
        except ModuleNotFoundError as error:
            if error.name not in ("numpy", "PIL"):
                raise
            self.skipTest(f"optional drawing dependency {error.name} is not installed")
        points = [[0, 0], [1, 0, 2], [2, 0, 2, 30], [3, 0, None, "rush"],
                  [4, 0, 2, "push:30"], [5, 0, 2, 0, "sack", 5, 0, 1],
                  [6, 0, None, 0]]
        overlays = route_overlays(points, lambda x, z: 0)
        labels = [item.text for item in overlays if isinstance(item, Text)]
        self.assertEqual(labels, ["0", "1 wait y 2", "2 hold 30", "3 rush",
                                  "4 push 30", "5 wait sack", "6"])


if __name__ == "__main__":
    unittest.main()
