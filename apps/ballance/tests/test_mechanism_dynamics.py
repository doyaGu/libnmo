"""Authored physics must reach native graphs, not merely source metadata."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ballance.route import dynamics

ROOT = Path(__file__).resolve().parents[1]


class DynamicsTests(unittest.TestCase):
    def test_strict_schema_and_default_identity(self):
        for kind in dynamics.profiles():
            defaults = dynamics.parameters(kind, {})
            self.assertIsNone(dynamics.variant('a', kind, defaults))
        for value in (
            {'force': True},
            {'force': float('nan')},
            {'force': float('inf')},
            {'force': -1},
            {'speed': 1},
            [],
            {'rotor_speed': 61},
        ):
            with self.assertRaises(ValueError):
                dynamics.parameters('P_Modul_18', value)
        for value in (
            {'limits_enabled': 1},
            {'limits_enabled': True, 'lower_angle': 0, 'upper_angle': 0},
            {'lower_angle': 1},
            {'upper_angle': -1},
        ):
            with self.assertRaises(ValueError):
                dynamics.parameters('P_Modul_17', value)
        with self.assertRaises(ValueError):
            dynamics.parameters('P_Modul_30', {'force': 1})


if __name__ == '__main__':
    unittest.main()
