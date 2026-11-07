"""Native floor material bands must survive free-form shape changes."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ballance.core.obj import Mesh, read_obj
from ballance.kit.rails import Ring
from ballance.kit.shapes import sweep, V_SECTION, FLAT_SECTION, inside
from ballance.route.build import build
import json


def sampled(width, first, last):
    mesh = Mesh()
    sweep(mesh, 0, 0, Ring(0, 0, 0, 1, 0), Ring(10, 0, 0, 1, 0), width, width, first, last, 0, 2)
    with tempfile.TemporaryDirectory() as tmp:
        file = Path(tmp) / 'floor.obj'
        mesh.write(file)
        return read_obj(file)


class FloorTextureTests(unittest.TestCase):
    def test_concave_platform_has_brick_edges_but_open_ports(self):
        course = {
            'start': {'heading': '+x'},
            'sectors': [
                [
                    {'road': 10},
                    {
                        'floor_polygon': {
                            'outline': [[0, -5], [10, -5], [10, -10], [30, -10], [30, 10], [0, 10]],
                            'entry': {'edge': 5, 'at': 2 / 3},
                            'exit': {'edge': 3},
                        }
                    },
                    {'road': 10},
                ]
            ],
        }
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            self.assertEqual(build(course, folder, None, 'Border'), 0)
            top = [
                t
                for t in read_obj(folder / 'sector1.obj')
                if t.material in ('Floor_Top_Border', 'Floor_Top_Boderless')
            ]
            painted = [t for t in top if t.material == 'Floor_Top_Border']
            self.assertTrue(painted)
            adapters = [
                t
                for t in read_obj(folder / 'sector1.obj')
                if t.material == 'Floor_Top_ProfileFlat' and all(24 < p[0] < 60 for p in t.points)
            ]
            # Each flattening is one native 2.5-unit cell, with a complete bitmap.
            self.assertEqual(len(adapters), 8)
            for t in adapters:
                self.assertAlmostEqual(
                    max(p[0] for p in t.points) - min(p[0] for p in t.points), 2.5
                )
                self.assertEqual({uv[1] for uv in t.uvs}, {0, 1})
            for port in ((27.39, 0), (57.39, 0)):
                self.assertFalse(
                    any(inside(port, [(p[0], p[2]) for p in t.points]) for t in painted)
                )
            # The border turns into both sides of each port. These points are
            # inside the mouth's concave corners, beyond the old truncated strips.
            for x in (27.49, 57.29):
                for z in (-2.2, 2.2):
                    self.assertTrue(
                        any(inside((x, z), [(p[0], p[2]) for p in t.points]) for t in painted),
                        f'open brick corner at {x, z}',
                    )
            area = 0
            for t in top:
                a, b, c = t.points
                area += abs((b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])) / 2
            self.assertAlmostEqual(area, 550)
            manifest = json.loads((folder / 'manifest.json').read_text())
            self.assertIn('Floor_Top_Border', {m['name'] for m in manifest['materials']})

    def test_adapter_uses_native_profile_to_flat_texture(self):
        for a, b in ((V_SECTION, FLAT_SECTION), (FLAT_SECTION, V_SECTION)):
            top = [t for t in sampled(5, a, b) if t.material.startswith('Floor_Top')]
            self.assertEqual({t.material for t in top}, {'Floor_Top_ProfileFlat'})

    def test_wide_profile_keeps_native_size_edges_and_tiles_the_interior(self):
        top = [t for t in sampled(10, V_SECTION, V_SECTION) if t.material.startswith('Floor_Top')]
        self.assertIn('Floor_Top_Boderless', {t.material for t in top})
        for t in top:
            if t.material == 'Floor_Top_Profile':
                self.assertLessEqual(max(p[2] for p in t.points) - min(p[2] for p in t.points), 2.5)

    def test_flat_texture_repeats_every_five_units_across_the_floor(self):
        top = [
            t for t in sampled(10, FLAT_SECTION, FLAT_SECTION) if t.material.startswith('Floor_Top')
        ]
        self.assertIn('Floor_Top_Border', {t.material for t in top})
        for triangle in top:
            for a, b, ua, ub in zip(
                triangle.points, triangle.points[1:], triangle.uvs, triangle.uvs[1:]
            ):
                if abs(a[0] - b[0]) < 1e-8:
                    self.assertAlmostEqual(abs(ua[0] - ub[0]) * 5, abs(a[2] - b[2]))


if __name__ == '__main__':
    unittest.main()
