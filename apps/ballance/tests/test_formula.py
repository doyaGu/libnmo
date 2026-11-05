"""Analytic paths, readable rail structure and recoverable fall volumes."""

import json
import math
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.formula import curve
from ballance.core.path import sample_path
from ballance.core.obj import Mesh, read_obj, write_box
from ballance.core.nmo import Nmo
from ballance.kit.architecture import ribs
from ballance.kit.rails import Ring
from ballance.route.build import build
from ballance.route.falls import fall_boxes, surface_cells, subtract, write_fall_zones
from ballance.route.manifest import level_object
from ballance.route.templates import mirror


class FormulaTests(unittest.TestCase):
    def test_rib_positions_do_not_depend_on_sampling_density(self):
        coarse, dense = Mesh(), Mesh()
        ribs(coarse, [Ring(x, 0, 0, 1, 0) for x in (0, 20)], 8, 3, True)
        ribs(dense, [Ring(x, 0, 0, 1, 0) for x in range(21)], 8, 3, True)
        self.assertEqual(coarse.lines, dense.lines)

    def test_wave_extrema_survive_sampling_and_join_straights_smoothly(self):
        points = sample_path(
            {'segments': [{'wave': {'length': 240, 'amplitude': 15, 'cycles': 4}}]}
        )
        self.assertGreater(max(s.point[1] for s in points), 14.99)
        self.assertLess(min(s.point[1] for s in points), -14.99)
        self.assertLess(math.dist(points[-1].point, (240, 0, 0)), 1e-10)
        self.assertLess(math.dist(points[-1].direction, (1, 0)), 1e-7)
        self.assertTrue(
            all(math.dist(a.point, b.point) <= 2.001 for a, b in zip(points, points[1:]))
        )

    def test_weave_has_distinct_height_at_each_centre_crossing(self):
        evaluate = curve('weave', {'length': 170, 'width': 100, 'lift': 16, 'rise': -12})
        crossings = [evaluate(t) for t in (0, 0.5, 1)]
        for p in crossings:
            self.assertLess(math.hypot(*p[:2]), 1e-10)
        self.assertEqual([p[2] for p in crossings], [0, 26, -12])

    def test_eased_turn_rotation_and_mirroring(self):
        spec = {
            'rail_path': {
                'segments': [
                    {'eased_turn': {'length': 100, 'angle': 90}},
                    {'wave': {'length': 150, 'amplitude': 12}},
                    {'weave': {'length': 100, 'width': 60}},
                ]
            }
        }
        a = sample_path(spec['rail_path'])
        b = sample_path(mirror(spec)['rail_path'])
        self.assertEqual(len(a), len(b))
        for p, q in zip(a, b):
            self.assertLess(math.dist(p.point, (q.point[0], -q.point[1], q.point[2])), 1e-6)
        self.assertLess(math.dist(a[-1].direction, (0, 1)), 1e-6)
        evaluate = curve('eased_turn', {'length': 100, 'angle': 90})
        x, y, _ = evaluate(1)
        self.assertAlmostEqual(x, y, places=8)

    def test_formula_parameters_reject_unbounded_or_aliasing_inputs(self):
        for kind, spec in [
            ('wave', {'cycles': 1.5}),
            ('wave', {'cycles': 9}),
            ('wave', {'amplitude': math.inf}),
            ('weave', {'hand': 0}),
            ('eased_turn', {'angle': 0}),
            ('wave', {'expression': 'x'}),
        ]:
            with self.subTest(kind=kind, spec=spec), self.assertRaises(ValueError):
                sample_path({'segments': [{kind: spec}]})

    def test_ribs_are_visible_without_physics_and_normals_can_be_imported(self):
        route = {
            'start': {'heading': '+x'},
            'sectors': [
                [
                    {'road': 10},
                    {
                        'rail_path': {
                            'segments': [{'wave': {'length': 140, 'amplitude': 10}}],
                            'ribs': {'spacing': 20, 'depth': 3},
                        }
                    },
                    {'road': 10},
                ]
            ],
        }
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            self.assertEqual(build(route, folder, None, 'Formula'), 0)
            objects = json.loads((folder / 'manifest.json').read_text())['scenes'][0]['objects']
            rib = next(o for o in objects if o['name'].startswith('Support_Ribs_'))
            self.assertEqual(rib['groups'], [])
            self.assertNotEqual(rib.get('visible'), False)
            self.assertTrue(read_obj(folder / rib['mesh']['obj']))
            if os.environ.get('NMO_TEST_CLI'):
                Nmo(os.environ['NMO_TEST_CLI']).run(
                    'patch',
                    'apply',
                    '--project',
                    str(folder / 'manifest.json'),
                    '-o',
                    str(folder / 'Level_01.NMO'),
                )

    def test_obj_writer_eliminates_float32_underflow_without_losing_map_precision(self):
        mesh = Mesh()
        mesh.triangle(
            'Rail',
            (0, 1, 0),
            ((900.00012, 0, 0), (0, 0), (2.14684e-48, 1, 6.12323e-17)),
            ((901, 0, 0), (1, 0), (0, 1, 0)),
            ((900, 0, 1), (0, 1), (0, 1, 0)),
        )
        self.assertIn('vn 0 1 0', mesh.lines)
        self.assertIn('v 900.00012 0 0', mesh.lines)


class DeathZoneTests(unittest.TestCase):
    def test_whole_long_rail_is_covered_and_decorative_tower_is_excluded(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            write_box(folder / 'rail.obj', (0, 250, 0, 1, 0, 4))
            write_box(folder / 'tower.obj', (0, 5, -160, 0, 0, 5))
            objects = [
                level_object('Rail', 'rail.obj', [0, 0, 0], ['Phys_FloorRails']),
                level_object('Tower_01', 'tower.obj', [0, 0, 0], ['Phys_Floors']),
            ]
            cells = surface_cells(objects, folder, 25)
            self.assertIn((137.5, 0, 12.5), cells)
            # A rolling ball may reach over a tile edge even when its
            # centre and supporting rail remain on this side of it.
            self.assertIn((137.5, 0, -12.5), cells)
            self.assertTrue(all(p[1] >= 0 for p in cells))
            boxes = fall_boxes(cells, 25, 18, 35, 5, 60)
            self.assertTrue(
                any(b[0] <= 125 <= b[1] and b[4] <= 2 <= b[5] and b[3] == -20 for b in boxes)
            )

    def test_lower_landing_keeps_its_local_column_clear(self):
        boxes = fall_boxes([(12, 40, 12), (12, 0, 12)], 25, 18, 35, 5, 60)
        self.assertTrue(all(b[3] <= -20 for b in boxes))

    def test_keepout_subtraction_preserves_volume_without_overlap(self):
        box, keep = (0, 10, 0, 10, 0, 10), (2, 8, 3, 7, 1, 9)
        parts = subtract(box, keep)

        def volume(box):
            return math.prod(box[k + 1] - box[k] for k in (0, 2, 4))

        self.assertAlmostEqual(sum(map(volume, parts)), volume(box) - volume(keep))
        for i, a in enumerate(parts):
            for b in [keep, *parts[i + 1 :]]:
                self.assertTrue(any(a[k + 1] <= b[k] or b[k + 1] <= a[k] for k in (0, 2, 4)))
        self.assertEqual(subtract(box, (20, 30, 20, 30, 20, 30)), [box])
        self.assertEqual(subtract(box, box), [])

    def test_policy_audit_and_invalid_recovery_volume(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            write_box(folder / 'floor.obj', (0, 20, 0, 1, 0, 5))
            objects = [level_object('Floor', 'floor.obj', [0, 0, 0], ['Phys_Floors'])]
            policy = {'keepouts': [{'box': [0, 5, -90, -10, 0, 5], 'reason': 'lower recovery pad'}]}
            meshes = write_fall_zones(objects, folder, policy)
            audit = json.loads((folder / 'death_zones.json').read_text())
            self.assertEqual(len(meshes), len(audit['boxes']))
            self.assertFalse(
                any(
                    b[0] < 2 < b[1] and b[2] < -40 < b[3] and b[4] < 2 < b[5]
                    for b in audit['boxes']
                )
            )
            with self.assertRaisesRegex(ValueError, 'reason'):
                write_fall_zones(objects, folder, {'keepouts': [{'box': [0, 1, 0, 1, 0, 1]}]})


if __name__ == '__main__':
    unittest.main()
