"""Native action evidence and expected-failure/recovery scenario verdicts."""

import copy
import csv
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.motion import rotate
from ballance.route.actions import compile_action
from ballance.route.gameplay import compile_contracts
from ballance.route.layout import Layout
from ballance.verify.actions import ActionTrace


def course():
    return {
        'schema_version': 1,
        'sectors': [
            [
                {'id': 'entry', 'road': 15},
                {'id': 'arm', 'module': 'P_Modul_17'},
                {'id': 'exit', 'road': 15},
            ]
        ],
    }


def action_spec():
    return {
        'mechanism': 'route/id/arm/mechanism/P_Modul_17/0',
        'rotation': {'body': 'Modul17_Dreharme', 'min_degrees': 30, 'direction': 'positive'},
        'hold': 5,
    }


class ActionTests(unittest.TestCase):
    def setUp(self):
        self.layout = Layout(course())
        self.action = compile_action(action_spec(), self.layout)
        self.guard = compile_action(
            {'mechanism': self.action['mechanism'], 'ball_region': [-10, 10, -10, 10, -10, 10]},
            self.layout,
        )
        self.spawn = self.action['instance']['position']
        self.checkpoint = [1000, 2, 0]
        self.case = {
            'id': 'fixture',
            'sector': 1,
            'ball': 'Wood',
            'spawn_world': self.spawn,
            'frames': 600,
            'expect': 'complete',
            'actions': [self.action],
            'guard_action': self.guard,
        }

    def write_trace(
        self, folder, angles, *, death=False, respawn=True, missing=(), body='Modul17_Dreharme'
    ):
        with (folder / 'steer.csv').open('w', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(['frame', 'x', 'y', 'z', 'object_id', 'object_name'])
            for frame in range(370, 600):
                position = (
                    self.checkpoint if death and respawn and 480 <= frame < 510 else self.spawn
                )
                writer.writerow([frame, *position, 9, 'Ball_Wood'])
        with (folder / 'physics.csv').open('w', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(['frame', 'id', 'name', *[f'm{k}' for k in range(16)]])
            for frame in range(370, 600):
                if frame in missing:
                    continue
                matrix = list(self.action['reference'])
                for offset in (0, 4, 8):
                    matrix[offset : offset + 3] = rotate(
                        matrix[offset : offset + 3],
                        (0, 0, 0),
                        self.action['axis'],
                        math.radians(angles(frame)),
                    )
                writer.writerow([frame, 77, body, *matrix])
        (folder / 'runtime.log').write_text(
            '[450] Send Message#1 sends "Ball Off" to All_Gameplay#2\n' if death else ''
        )

    def test_completion_requires_sustained_rotation_in_the_requested_direction(self):
        for angle, duration, passed in ((60, 10, True), (-60, 10, False), (60, 2, False)):
            with self.subTest(angle=angle, duration=duration), tempfile.TemporaryDirectory() as tmp:
                folder = Path(tmp)
                self.write_trace(folder, lambda f: angle if 400 <= f < 400 + duration else 0)
                trace = ActionTrace(folder, [self.action])
                result = trace.inspect(self.action, 370, 599)
                self.assertEqual(result['status'] == 'passed', passed)
                self.assertTrue(result['complete_window'])
                if passed:
                    self.assertEqual(result['observed']['runtime_id'], '77')
                    self.assertEqual(result['observed']['frame'], 404)

    def test_body_at_wrong_instance_and_nonfinite_trace_cannot_satisfy_action(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            self.write_trace(folder, lambda f: 60 if f >= 400 else 0)
            far = copy.deepcopy(self.action)
            far['reference'][12] += 100
            self.assertEqual(ActionTrace(folder, [far]).inspect(far, 370, 599)['samples'], 0)
            text = (folder / 'steer.csv').read_text().replace('370,', '370,nan,', 1)
            (folder / 'steer.csv').write_text(text)
            with self.assertRaises((ValueError, TypeError)):
                ActionTrace(folder, [self.action])

    def test_invalid_body_pose_is_not_a_successful_negative_test(self):
        from ballance.verify.actions import hinge_degrees

        for matrix in ([0] * 16, [1] * 16):
            with self.subTest(matrix=matrix), self.assertRaisesRegex(ValueError, 'basis'):
                hinge_degrees(matrix, self.action['reference'], self.action['axis'])

    def test_lift_requires_the_same_ball_to_rise_from_the_lower_region_in_time(self):
        spec = {
            'mechanism': self.action['mechanism'],
            'ball_region': [-4, 4, 12, 15, -4, 4],
            'hold': 5,
            'rise': {'from': [-4, 4, 0, 1, -4, 4], 'min_height': 10, 'within': 180},
        }
        for falling, swap, gap, timeout, passed in (
            (False, False, False, False, True),
            (True, False, False, False, False),
            (False, True, False, False, False),
            (False, False, True, False, False),
            (False, False, False, True, False),
        ):
            action = compile_action(spec, self.layout)
            if timeout:
                action['rise']['within'] = 50
            with (
                self.subTest(falling=falling, swap=swap, gap=gap, timeout=timeout),
                tempfile.TemporaryDirectory() as tmp,
            ):
                folder = Path(tmp)
                with (folder / 'steer.csv').open('w', newline='') as stream:
                    writer = csv.writer(stream)
                    writer.writerow(['frame', 'x', 'y', 'z', 'object_id', 'object_name'])
                    x, base, z = self.spawn
                    for frame in range(370, 600):
                        if gap and frame == 450:
                            continue
                        height = 20 - (frame - 370) * 0.1 if falling else (frame - 370) * 0.1
                        writer.writerow(
                            [
                                frame,
                                x,
                                base + height,
                                z,
                                10 if swap and frame >= 450 else 9,
                                'Ball_Wood',
                            ]
                        )
                observed = ActionTrace(folder, [action]).inspect(action, 370, 599)
                self.assertEqual(observed['status'] == 'passed', passed)
                if passed:
                    self.assertGreaterEqual(observed['observed']['rise'], 10)
                    self.assertEqual(observed['observed']['from_frame'], 370)

    def test_action_contracts_resolve_identity_and_reject_invalid_conditions(self):
        route = course()
        route['gameplay'] = {'contracts': [{'id': 'turn', 'action': action_spec()}]}
        compiled = compile_contracts(route, self.layout)['contracts'][0]
        self.assertEqual(compiled['sector'], 1)
        self.assertEqual(compiled['kind'], 'action')
        for change in (
            {'mechanism': 'missing'},
            {'hold': True},
            {'rotation': {'body': 'unknown', 'min_degrees': 30}},
            {'rotation': {'body': 'Modul17_Dreharme', 'min_degrees': float('nan')}},
            {'ball_region': [0, 1, 0, 1, 0, 1]},
        ):
            with self.subTest(change=change), self.assertRaises(ValueError):
                compile_action(action_spec() | change, self.layout)


if __name__ == '__main__':
    unittest.main()
