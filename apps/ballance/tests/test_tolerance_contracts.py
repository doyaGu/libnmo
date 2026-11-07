"""Map-independent perturbation compilation, evidence and report regressions."""

import copy
import csv
import itertools
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core import evidence, project
from ballance.core.motion import rotate
from ballance.route.layout import Layout
from ballance.verify import tolerance
from ballance.verify.replay import evaluate_landing
from ballance.verify.scenarios import first_frame
from verification_fixtures import message, synthetic_runtime, write_ball_trace


def course():
    return {
        'schema_version': 1,
        'start': {'heading': '+x'},
        'sectors': [
            [
                {'id': 'entry', 'road': 10},
                {'id': 'arm', 'module': 'P_Modul_17'},
                {'id': 'exit', 'road': 20},
            ]
        ],
        'passages': [
            {
                'id': 'drop',
                'owner': 'route/id/exit',
                'kind': 'landing',
                'points': [[0, 0, 2], [8, 0, -2]],
                'landing_radius': 1,
            }
        ],
    }


def scenarios():
    mechanism = 'route/id/arm/mechanism/P_Modul_17/0'
    return {
        'version': 1,
        'scenarios': [
            {
                'id': 'turn',
                'mechanism': mechanism,
                'ball': 'Wood',
                'spawn': [0, 2, 0],
                'points': [[5, 2, 0]],
                'frames': 600,
                'expect': 'complete',
                'guard': {'mechanism': mechanism, 'ball_region': [-5, 5, -5, 5, -5, 5]},
                'conditions': [
                    {
                        'mechanism': mechanism,
                        'rotation': {'body': 'Modul17_Dreharme', 'min_degrees': 30},
                    }
                ],
            }
        ],
    }


def experiment(**options):
    return {
        'id': 'probe',
        'scenario': 'turn',
        'heading': [3, 4],
        'axes': {axis: [0] for axis in tolerance.AXES},
        **options,
    }


def specification(*experiments):
    return {'version': 1, 'suite': 'scenarios.json', 'experiments': list(experiments)}


class ToleranceCompilationTests(unittest.TestCase):
    def test_axes_and_product_keep_one_zero_control_and_explicit_combinations(self):
        axes = {
            'entry_speed': [4, 0],
            'lateral_offset': [-1, 0, 1],
            'timing_frames': [2, 0],
            'phase_frames': [10, 0],
        }
        combined = {'entry_speed': 4, 'lateral_offset': -1}
        spec = experiment(axes=axes, combinations=[combined, combined])
        zero = tolerance.parameter_values({})
        samples = tolerance.sample_parameters(spec)
        expected = [zero]
        for axis, values in axes.items():
            expected.extend(tolerance.parameter_values({axis: v}) for v in values if v)
        expected.append(tolerance.parameter_values(combined))
        self.assertEqual(samples, expected)
        product = tolerance.sample_parameters(spec | {'sampling': 'product'})
        self.assertEqual(product[0], zero)
        self.assertEqual(len(product), 24)
        self.assertEqual(
            {tuple(p[a] for a in tolerance.AXES) for p in product},
            set(itertools.product(*axes.values())),
        )

    def test_invalid_axes_and_unbounded_grids_are_rejected(self):
        for values in ([1], [0, 0], [], [0, True], [0, float('nan')], [0, 61]):
            spec = experiment()
            spec['axes']['entry_speed'] = values
            with self.subTest(values=values), self.assertRaises(ValueError):
                tolerance.sample_parameters(spec)
        for parameters in (
            {'unknown': 1},
            {'timing_frames': 0.5},
            {'phase_frames': -1},
            {'lateral_offset': 11},
            {'entry_speed': float('inf')},
        ):
            with self.subTest(parameters=parameters), self.assertRaises(ValueError):
                tolerance.parameter_values(parameters)
        spec = experiment(axes={axis: list(range(5)) for axis in tolerance.AXES})
        with self.assertRaisesRegex(ValueError, '256'):
            tolerance.sample_parameters(spec | {'sampling': 'product'})
        spec = experiment(combinations=[{}] * 33)
        with self.assertRaisesRegex(ValueError, '32'):
            tolerance.sample_parameters(spec)

    def test_compilation_resolves_mechanisms_and_normalizes_heading(self):
        layout = Layout(course())
        suite = scenarios()
        original = copy.deepcopy(suite)
        compiled = tolerance.compile_experiments(specification(experiment()), suite, layout)[0]
        self.assertEqual(compiled['heading'], [0.6, 0.8])
        case = compiled['case']
        self.assertEqual(case['actions'][0]['instance']['id'], suite['scenarios'][0]['mechanism'])
        self.assertEqual(case['sector'], 1)
        case['spawn_world'] = [0, 0, 0]
        self.assertEqual(suite, original)

    def test_negative_and_checkpoint_cases_cannot_be_tolerance_references(self):
        for start, expect in (('probe', 'blocked'), ('checkpoint', 'complete')):
            suite = scenarios()
            case = suite['scenarios'][0]
            case.update(start=start, expect=expect)
            if start == 'checkpoint':
                del case['spawn']
            with self.subTest(start=start), self.assertRaisesRegex(ValueError, 'positive probe'):
                tolerance.compile_experiments(specification(experiment()), suite, Layout(course()))

    def test_invalid_references_and_duplicate_experiment_ids_are_rejected(self):
        invalid = [
            specification(experiment(scenario='absent')),
            specification(experiment(heading=[0, 0])),
            specification(experiment(), experiment()),
            specification(experiment(landing='drop')),
        ]
        for spec in invalid:
            with self.subTest(spec=spec), self.assertRaises(ValueError):
                tolerance.compile_experiments(spec, scenarios(), Layout(course()))

    def test_landing_uses_owner_frame_and_forbids_mechanism_phase(self):
        spec = experiment(landing='drop')
        del spec['scenario']
        layout = Layout(course())
        compiled = tolerance.compile_experiments(specification(spec), scenarios(), layout)[0]
        pose = layout.entry_poses['route/id/exit']['position']
        self.assertEqual(compiled['case']['spawn_world'], [pose[0] - 8, pose[1] + 2, pose[2]])
        self.assertEqual(compiled['case']['landing']['id'], 'drop')
        spec['axes']['phase_frames'] = [0, 10]
        with self.assertRaisesRegex(ValueError, 'static landing'):
            tolerance.compile_experiments(specification(spec), scenarios(), layout)

    def test_perturbation_separates_offset_velocity_timing_and_world_phase(self):
        compiled = tolerance.compile_experiments(
            specification(experiment()), scenarios(), Layout(course())
        )[0]
        original = copy.deepcopy(compiled)
        parameters = tolerance.parameter_values(
            {
                'entry_speed': 5,
                'lateral_offset': 2,
                'timing_frames': -4,
                'phase_frames': 30,
            }
        )
        case = tolerance.perturb_case(compiled, parameters, 2)
        spawn = original['case']['spawn_world']
        self.assertEqual(case['spawn_world'], [spawn[0] + 1.6, spawn[1], spawn[2] - 1.2])
        self.assertEqual(
            case['probe_options'],
            {
                'probe_velocity': [3, 0, 4],
                'probe_delay_frames': 30,
                'probe_rehearsal_position': list(spawn),
            },
        )
        self.assertEqual(case['first_frame'], first_frame(original['case']) + 30)
        self.assertEqual(case['frames'], original['case']['frames'] + 30)
        self.assertIn('Modul17_Dreharme*', case['observe'])
        self.assertEqual(compiled, original)


class TolerancePhaseTests(unittest.TestCase):
    def setUp(self):
        compiled = tolerance.compile_experiments(
            specification(experiment()), scenarios(), Layout(course())
        )[0]
        self.case = tolerance.perturb_case(
            compiled, tolerance.parameter_values({'phase_frames': 30}), 1
        )
        action = self.case['actions'][0]
        rest = list(action['reference'])
        turned = list(rest)
        for offset in (0, 4, 8):
            turned[offset : offset + 3] = rotate(
                rest[offset : offset + 3], (0, 0, 0), action['axis'], math.radians(37)
            )
        self.body = {'id': 77, 'name': action['body'], 'matrix': rest}
        self.snapshots = [
            {'frame': 370, 'entities': [self.body]},
            {'frame': 400, 'entities': [self.body | {'matrix': turned}]},
        ]

    def observations(self, snapshots):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            project.write_json(folder / 'observations.json', snapshots)
            return tolerance.release_states(folder, self.case)

    def test_release_tracks_bound_runtime_identity_and_actual_angle(self):
        # A new body at the original pose must not replace the moving body.
        self.snapshots[1]['entities'].append(self.body | {'id': 99})
        observed = self.observations(self.snapshots)
        self.assertEqual(len(observed), 1)
        self.assertEqual(observed[0]['runtime_id'], 77)
        self.assertEqual(observed[0]['frame'], 400)
        self.assertAlmostEqual(observed[0]['degrees'], 37)

    def test_ambiguous_binding_or_missing_original_body_cannot_supply_phase_evidence(self):
        ambiguous = copy.deepcopy(self.snapshots)
        ambiguous[0]['entities'].append(self.body | {'id': 99})
        missing = copy.deepcopy(self.snapshots)
        missing[1]['entities'] = [self.body | {'id': 99}]
        for snapshots, reason in ((ambiguous, 'cannot bind'), (missing, 'pose is missing')):
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, reason):
                self.observations(snapshots)


class ToleranceEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.case = {
            'id': 'drop',
            'sector': 1,
            'first_frame': 370,
            'frames': 450,
            'ball': 'Wood',
            'spawn_world': [0, 6, 0],
            'parameters': tolerance.parameter_values({}),
            'landing': {'points': [[0, 6, 0], [4, 2, 0]], 'landing_radius': 1},
        }
        self.write_trace()

    def write_trace(self, death=None, identity_break=False):
        rows = []
        for frame in range(370, 430):
            position = [0, 6, 0] if frame < 390 else [4, 2, 0]
            if death is not None and frame >= 420:
                position = [40, 2, 0]
            rows.append((frame, position, 8 if identity_break and frame >= 390 else 7, 'Ball_Wood'))
        write_ball_trace(self.folder, rows)
        (self.folder / 'runtime.log').write_text(message(death, 'Ball Off') if death else '')
        project.write_json(
            self.folder / 'observations.json',
            [{'frame': 370, 'entities': [], 'gameplay': {'deaths': 0}}],
        )

    def measured(self):
        return tolerance.measure(
            self.folder, self.case, evaluate_landing(self.folder, self.case), [40, 2, 0]
        )

    def test_death_before_arrival_is_not_success_after_respawn(self):
        self.write_trace(death=380)
        result = self.measured()
        self.assertEqual(result['outcome'], 'death_before_completion')
        self.assertFalse(result['goal_completed'])
        self.assertEqual(result['respawn_frames'], 40)

    def test_death_after_completion_still_requires_review(self):
        self.write_trace(death=400)
        result = self.measured()
        self.assertEqual(result['outcome'], 'completed_then_died')
        self.assertTrue(result['goal_completed'])
        self.assertEqual(result['completion_frames'], 22)

    def test_identity_break_cannot_confirm_a_landing(self):
        self.assertEqual(self.measured()['outcome'], 'success')
        self.write_trace(identity_break=True)
        self.assertEqual(self.measured()['outcome'], 'condition_not_met')

    def test_missing_spawn_and_prelude_death_invalidate_the_sample(self):
        result = evaluate_landing(self.folder, self.case)
        result['spawn_observed'] = False
        measured = tolerance.measure(self.folder, self.case, result, [40, 2, 0])
        self.assertEqual(measured['outcome'], 'invalid')
        self.case['parameters']['phase_frames'] = 20
        self.write_trace(death=360)
        project.write_json(
            self.folder / 'observations.json',
            [
                {'frame': 350, 'entities': [], 'gameplay': {'deaths': 0}},
                {'frame': 370, 'entities': [], 'gameplay': {'deaths': 0}},
            ],
        )
        self.assertEqual(self.measured()['outcome'], 'invalid')

    def test_summary_distinguishes_failed_control_invalid_evidence_and_sample_failures(self):
        zero = {'parameters': tolerance.parameter_values({}), 'outcome': 'success'}
        varied = {
            'parameters': tolerance.parameter_values({'entry_speed': 2}),
            'outcome': 'success',
        }
        for baseline, outcome, expected in (
            ('success', 'success', 'passed'),
            ('success', 'condition_not_met', 'review'),
            ('success', 'invalid', 'failed'),
            ('condition_not_met', 'success', 'failed'),
        ):
            with self.subTest(baseline=baseline, outcome=outcome):
                report = tolerance.summarize(
                    {'id': 'grid'},
                    [
                        zero | {'outcome': baseline},
                        varied | {'outcome': outcome},
                    ],
                )
                self.assertEqual(report['status'], expected)
                self.assertEqual(report['sample_count'], 2)


class ToleranceRunnerTests(unittest.TestCase):
    def test_zero_control_requires_identical_physics_and_reports_keep_evidence(self):
        for changed in (False, True):
            with self.subTest(changed=changed), synthetic_runtime(tolerance, course()) as runtime:
                spec = experiment(landing='drop', heading=[1, 0])
                del spec['scenario']
                project.write_json(runtime.specification, specification(spec))
                project.write_json(runtime.root / 'scenarios.json', scenarios())
                calls = []

                def execute(source, level, folder, case, nmo, game, checkpoint, keys=None):
                    folder.mkdir(parents=True, exist_ok=True)
                    first = first_frame(case)
                    positions = [case['spawn_world'], *case['landing']['points']]
                    write_ball_trace(
                        folder,
                        [(first + i, positions[min(i, 2)], 7, 'Ball_Wood') for i in range(40)],
                    )
                    (folder / 'physics.csv').write_text('changed' if changed and keys else 'same')
                    (folder / 'runtime.log').write_text('')
                    project.write_json(folder / 'input.json', {'synthetic': True})
                    project.write_json(folder / 'keys.json', keys)
                    project.write_json(
                        folder / 'observations.json',
                        [{'frame': first, 'entities': [], 'gameplay': {'deaths': 0}}],
                    )
                    calls.append(keys)
                    return evaluate_landing(folder, case)

                with patch.object(tolerance, 'run_case', side_effect=execute):
                    report = tolerance.run_tolerance(
                        runtime.source,
                        runtime.specification,
                        runtime.out,
                        runtime.nmo,
                        runtime.game,
                    )
                self.assertIsNone(calls[0])
                self.assertEqual(calls[1][370], 1)
                self.assertEqual(report['status'], 'failed' if changed else 'passed')
                sample = report['experiments'][0]['samples'][0]
                self.assertEqual(sample['reference_physics_identical'], not changed)
                self.assertEqual(sample['outcome'], 'invalid' if changed else 'success')
                for name in ('tolerance.json', 'tolerance.csv', 'probe-000/validation.json'):
                    self.assertEqual(evidence.inspect(runtime.out / name)['status'], 'current')
                with (runtime.out / 'tolerance.csv').open() as stream:
                    rows = list(csv.DictReader(stream))
                self.assertEqual(rows[0]['outcome'], sample['outcome'])

    def test_failed_reference_clears_old_success_and_never_runs_samples(self):
        with synthetic_runtime(tolerance, course()) as runtime:
            project.write_json(runtime.specification, specification(experiment()))
            project.write_json(runtime.root / 'scenarios.json', scenarios())
            runtime.out.mkdir()
            report = runtime.out / 'tolerance.json'
            project.write_json(report, {'status': 'passed'})
            sidecar = Path(str(report) + '.evidence.json')
            sidecar.write_text('{}')

            def failed_reference(source, level, folder, *args):
                folder.mkdir()
                return {'status': 'failed'}

            with patch.object(tolerance, 'run_case', side_effect=failed_reference) as execute:
                with self.assertRaisesRegex(ValueError, 'reference solution failed'):
                    tolerance.run_tolerance(
                        runtime.source,
                        runtime.specification,
                        runtime.out,
                        runtime.nmo,
                        runtime.game,
                    )
                self.assertEqual(execute.call_count, 1)
            self.assertEqual(json.loads(report.read_text())['status'], 'failed')
            self.assertFalse(sidecar.exists())


if __name__ == '__main__':
    unittest.main()
