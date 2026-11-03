"""Tests for extracting level meshes."""

import tempfile
from unittest.mock import patch
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.extract import elements


class BallanceTests(unittest.TestCase):
    def test_chain_cut_reports_each_written_mesh(self):
        cut = elements.ChainCut("Log", "Level_01.NMO", "floor", (0, 1, 0, 1, 0, 1),
                                (0, 1, 0, 1, 0, 1), 1, 2)

        class Levels:
            by_name = {"level_01.nmo": Path("Level_01.NMO")}
            files = []

            def objects(self, path):
                return {"floor": {"world_matrix": "(1,0,0,0;0,1,0,0;0,0,1,0;0,0,0,1)"}}

            def faces(self, path, obj):
                return [], ""

        with tempfile.TemporaryDirectory() as folder, \
                patch.object(elements, "CUTS", [cut]), patch.object(elements, "KINDS", []):
            found = elements.export_elements(Levels(), Path(folder))
            self.assertEqual(set(found), set(elements.every_kind()))
            self.assertTrue(all((Path(folder) / f"{name}.obj").is_file() for name in found))
            self.assertEqual(found["Log_1"], "Level_01.NMO:floor")

            class MissingFloor(Levels):
                def objects(self, path):
                    return {}

            self.assertEqual(elements.export_elements(MissingFloor(), Path(folder)), {})


if __name__ == "__main__":
    unittest.main()
