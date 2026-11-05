"""Authored gameplay conditions must be supported by runtime observations."""

import csv
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ballance.route.gameplay import compile_contracts
from ballance.route.layout import Layout
from ballance.verify.gameplay import inspect


class GameplayTests(unittest.TestCase):
    def course(self, ball='Wood'):
        return {
            'schema_version': 1,
            'sectors': [[{'id': 'gate', 'road': 20}]],
            'gameplay': {
                'contracts': [
                    {'id': 'ready', 'at': 'route/id/gate', 'ball': ball},
                    {'id': 'finish', 'event': 'Level_Finish', 'sector': 1, 'after': ['ready']},
                ]
            },
        }

    def run_trace(self, rows, events, course=None, sector=1):
        course = course or self.course()
        spec = compile_contracts(course, Layout(course))
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            with (folder / 'steer.csv').open('w') as f:
                writer = csv.writer(f)
                writer.writerow(['frame', 'x', 'y', 'z', 'object_id', 'object_name'])
                for frame, ball, point in rows:
                    writer.writerow([frame, *point, 99, 'Ball_' + ball])
            (folder / 'run.log').write_text(
                '\n'.join(
                    f'[{frame}] Send Message#12 sends "{name}" to {target}'
                    for frame, name, target in events
                )
            )
            return inspect(folder, spec, sector=sector)

    def test_wrong_ball_missing_passage_and_early_finish_fail(self):
        p = compile_contracts(self.course(), Layout(self.course()))['contracts'][0]['position']
        finish = [(400, 'Level_Finish', 'All_Gameplay#23')]
        self.assertEqual(self.run_trace([(380, 'Wood', p)], finish)['status'], 'passed')
        wrong = self.run_trace([(380, 'Stone', p)], finish)
        self.assertEqual(wrong['status'], 'failed')
        self.assertIn('Wrong ball', wrong['contracts'][0]['reason'])
        missing = self.run_trace([(380, 'Wood', [1000, 1000, 1000])], finish)
        self.assertEqual(missing['status'], 'failed')
        early = self.run_trace([(420, 'Wood', p)], finish)
        self.assertEqual(early['contracts'][1]['unmet'], ['ready'])
        # A later successful visit cannot erase the first wrong-ball entry.
        self.assertEqual(
            self.run_trace([(380, 'Stone', p), (390, 'Wood', p)], finish)['status'], 'failed'
        )

    def test_sound_messages_and_cross_layer_proximity_do_not_satisfy_contract(self):
        p = compile_contracts(self.course(), Layout(self.course()))['contracts'][0]['position']
        self.assertEqual(
            self.run_trace([(380, 'Wood', p)], [(400, 'Level_Finish', 'All_Sound#23')])['status'],
            'failed',
        )
        p[1] += 10
        self.assertEqual(self.run_trace([(380, 'Wood', p)], [])['contracts'][0]['status'], 'failed')

    def test_invalid_references_cycles_and_unknown_conditions_fail_at_build_time(self):
        cases = [
            {'after': ['finish']},
            {'at': 'missing'},
            {'radius': True},
            {'radius': float('nan')},
            {'ball': 'Metal'},
            {'sector': 1},
            {'unknown': True},
        ]
        for patch in cases:
            with self.subTest(patch=patch):
                course = self.course()
                course['gameplay']['contracts'][0].update(patch)
                with self.assertRaises(ValueError):
                    compile_contracts(course, Layout(course))

    def test_sector_events_gate_dependencies_and_partial_runs_record_assumptions(self):
        from ballance.verify.play import steer_from

        course = self.course()
        course['sectors'].append([{'id': 'second', 'road': 20}])
        course['gameplay']['contracts'] = [
            {'id': 'checkpoint', 'event': 'Checkpoint reached', 'sector': 1},
            {'id': 'second-gate', 'at': 'route/id/second', 'ball': 'Wood', 'after': ['checkpoint']},
            {'id': 'finish', 'event': 'Level_Finish', 'sector': 2, 'after': ['second-gate']},
        ]
        p = compile_contracts(course, Layout(course))['contracts'][1]['position']
        events = [
            (400, 'Checkpoint reached', 'All_Gameplay#23'),
            (500, 'Level_Finish', 'All_Gameplay#23'),
        ]
        self.assertEqual(self.run_trace([(450, 'Wood', p)], events, course)['status'], 'passed')
        # Visiting the same place before its sector activates is not passage.
        self.assertEqual(self.run_trace([(390, 'Wood', p)], events, course)['status'], 'failed')
        first = steer_from(2)
        partial = self.run_trace(
            [(first + 10, 'Wood', p)], [(first + 20, 'Level_Finish', 'All_Gameplay#23')], course, 2
        )
        self.assertEqual(partial['status'], 'passed')
        self.assertEqual(partial['contracts'][0]['status'], 'skipped')

    def test_unobserved_ball_identity_cannot_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / 'run.log').write_text('')
            (folder / 'steer.csv').write_text('frame,x,y,z\n380,0,0,0\n')
            with self.assertRaisesRegex(ValueError, 'actual object identity'):
                inspect(folder, compile_contracts(self.course(), Layout(self.course())))

    @unittest.skipUnless(
        os.environ.get('NMO_TEST_PHYSICS') == '1'
        and os.environ.get('BALLANCE')
        and os.environ.get('BALLANCE_ELEMENTS')
        and os.environ.get('NMO_TEST_CLI'),
        'needs native physics resources',
    )
    def test_native_runtime_rejects_wrong_ball_even_when_level_finishes(self):
        from ballance.core.nmo import Nmo
        from ballance.route.build import build
        from ballance.verify.play import play

        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            route = self.course('Paper')
            build(route, folder, Path(os.environ['BALLANCE_ELEMENTS']), 'Wrong ball')
            result = play(
                folder, 1600, Nmo(os.environ['NMO_TEST_CLI']), Path(os.environ['BALLANCE'])
            )
            self.assertIsNotNone(result.finished)
            spec = json.loads((folder / 'gameplay_contracts.json').read_text())
            report = inspect(folder, spec, finish=result.finished)
            self.assertEqual(report['status'], 'failed')
            self.assertEqual(report['contracts'][0]['violation']['object_name'], 'Ball_Wood')
            good = self.course('Wood')
            self.assertEqual(
                inspect(folder, compile_contracts(good, Layout(good)), finish=result.finished)[
                    'status'
                ],
                'passed',
            )


if __name__ == '__main__':
    unittest.main()
