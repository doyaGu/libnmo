"""Fixed native input replay, death observations and physics trace identity."""

import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.verify.replay import (
    ball_trace,
    death_frames,
    evaluate_landing,
    input_ranges,
    shift_inputs,
)


class ReplayTests(unittest.TestCase):
    def test_fixed_keys_keep_release_and_press_time_independent(self):
        reference = {frame: (1 if frame < 105 else 8) for frame in range(100, 130)}
        nominal = shift_inputs(reference, 100, 140)
        late = shift_inputs(reference, 100, 140, timing=3)
        early = shift_inputs(reference, 100, 140, timing=-3)
        phase = shift_inputs(reference, 100, 170, phase=30)
        self.assertEqual(nominal[105], 8)
        self.assertEqual(late[105], 1)
        self.assertEqual(early[102], 8)
        self.assertEqual(phase[105], 8)  # native prelude
        self.assertEqual(phase[130], 1)  # replay begins again, world stays alive
        self.assertEqual(phase[135], 8)
        self.assertEqual(
            input_ranges({0: 0, 1: 1, 2: 1, 3: 0}),
            [
                {'first': 0, 'last': 0, 'mask': 0},
                {'first': 1, 'last': 2, 'mask': 1},
                {'first': 3, 'last': 3, 'mask': 0},
            ],
        )

    def test_death_snapshots_cut_off_landing_success_after_a_retry(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'runtime.log').write_text('')
            (folder / 'observations.json').write_text(
                json.dumps(
                    [
                        {'frame': 391, 'gameplay': {'deaths': 2, 'death_frame': 390}},
                        {'frame': 450, 'gameplay': {'deaths': 2, 'death_frame': 390}},
                    ]
                )
            )
            with (folder / 'steer.csv').open('w', newline='') as stream:
                writer = csv.writer(stream)
                writer.writerow(['frame', 'x', 'y', 'z', 'object_id', 'object_name'])
                for frame in range(370, 450):
                    position = [0, 10, 0] if frame < 400 else [5, 2, 0]
                    writer.writerow([frame, *position, 7, 'Ball_Wood'])
            case = {
                'id': 'landing',
                'ball': 'Wood',
                'first_frame': 370,
                'frames': 450,
                'spawn_world': [0, 10, 0],
                'landing': {'points': [[0, 10, 0], [5, 2, 0]], 'landing_radius': 1},
            }
            self.assertEqual(death_frames(folder, 370), [390])
            self.assertEqual(death_frames(folder, 400), [])
            result = evaluate_landing(folder, case)
            self.assertTrue(result['spawn_observed'])
            self.assertEqual(result['landing']['status'], 'failed')
            (folder / 'observations.json').unlink()
            self.assertEqual(evaluate_landing(folder, case)['status'], 'passed')

    def test_physics_ball_trace_preserves_identity_gaps_and_frame_boundary(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            with (folder / 'physics.csv').open('w', newline='') as stream:
                writer = csv.writer(stream)
                writer.writerow(['frame', 'id', 'name', *[f'm{k}' for k in range(16)]])
                for frame, name in (
                    (10, 'Ball_Wood_piece01'),
                    (10, 'Ball_Wood'),
                    (12, 'Ball_Paper'),
                ):
                    writer.writerow([frame, 7, name, *([0] * 12), 1, 2, 3, 1])
            ball_trace(folder)
            with (folder / 'steer.csv').open() as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual([r['frame'] for r in rows], ['11', '13'])
            self.assertEqual([r['object_name'] for r in rows], ['Ball_Wood', 'Ball_Paper'])


if __name__ == '__main__':
    unittest.main()
