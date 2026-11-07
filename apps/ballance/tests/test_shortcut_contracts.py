"""Authored shortcut compilation and report checks using tiny synthetic routes."""

import copy
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core import evidence, project
from ballance.route.layout import Layout
from ballance.verify import shortcuts
from verification_fixtures import message, synthetic_runtime, write_ball_trace


def course(heading='+x'):
    return {
        'schema_version': 1,
        'start': {'heading': heading},
        'sectors': [
            [{'id': name, 'road': 10} for name in ('a', 'b', 'c', 'd', 'e')],
            [{'id': 'exit', 'road': 10}],
        ],
        'gameplay': {
            'contracts': [
                {'id': 'checkpoint', 'event': 'Checkpoint reached', 'sector': 1},
                {'id': 'finish', 'event': 'Level_Finish', 'sector': 2, 'after': ['checkpoint']},
            ]
        },
    }


def reference(owner, *coordinates, **options):
    return {'owner': f'route/id/{owner}', 'at': list(coordinates), **options}


def shortcut(identifier='cut', start='a', end='c'):
    return {
        'id': identifier,
        'leave_after': reference(start, 0, 0),
        'resume_before': reference(end, 0, 0),
        'waypoints': [reference(start, 10, 2, drive=12)],
        'departure': reference(start, 0, 0, 6),
        'landing': reference(end, 0, 0, 2),
        'landing_radius': 1,
        'ball': 'Wood',
        'minimum_saved_frames': 10,
        'tradeoff': 'A narrower landing in exchange for a shorter path.',
    }


def specification(*cuts):
    return {'version': 1, 'shortcuts': list(cuts)}


class ShortcutCompilationTests(unittest.TestCase):
    def test_owner_coordinates_rotate_with_the_route(self):
        for heading, direction in (
            ('+x', (1, 0)),
            ('+z', (0, 1)),
            ('-x', (-1, 0)),
            ('-z', (0, -1)),
        ):
            with self.subTest(heading=heading):
                layout = Layout(course(heading))
                cut = shortcuts.compile_shortcuts(specification(shortcut()), layout)[0]
                x, y, z = layout.entry_poses['route/id/a']['position']
                dx, dz = direction
                self.assertEqual(
                    cut['points'],
                    [[x + 10 * dx + 2 * dz, z + 10 * dz - 2 * dx, 1000000, 'push:12']],
                )
                self.assertEqual(cut['passage']['points'][0], [x, y + 6, z])
                self.assertEqual(cut['sector'], 1)

    def test_splicing_preserves_anchors_checkpoints_suffix_and_source(self):
        layout = Layout(course())
        original = copy.deepcopy(layout.points)
        values = specification(shortcut('late', 'c', 'e'), shortcut('early', 'a', 'c'))
        cuts = shortcuts.compile_shortcuts(values, layout)
        first, second = cuts
        self.assertEqual([c['id'] for c in cuts], ['early', 'late'])
        joined = shortcuts.route_points(layout.points, list(reversed(cuts)))
        self.assertEqual(
            joined,
            [
                *original[: first['start'] + 1],
                *first['points'],
                original[first['end']],
                *second['points'],
                *original[second['end'] :],
            ],
        )
        checkpoint = layout.sector_starts[1]['point']
        self.assertEqual(joined[-len(original[checkpoint:]) :], original[checkpoint:])
        self.assertEqual(layout.points, original)
        self.assertEqual(shortcuts.route_points(original, []), original)

    def test_repeated_xz_needs_an_explicit_occurrence(self):
        layout = Layout(course())
        sectors = {p.origin.entity_id: p.sector for p in layout.parts if p.origin}
        point = reference('a', 0, 0)
        index, _ = shortcuts.anchor(point, layout, sectors)
        # Two visits in the same sector, as in a loop or a layered crossing.
        layout.points[index + 1] = list(layout.points[index])
        with self.assertRaisesRegex(ValueError, 'unique or specify occurrence'):
            shortcuts.anchor(point, layout, sectors)
        self.assertEqual(
            shortcuts.anchor(point | {'occurrence': 1}, layout, sectors), (index + 1, 1)
        )
        for occurrence in (2, -1, True):
            with self.subTest(occurrence=occurrence), self.assertRaises(ValueError):
                shortcuts.anchor(point | {'occurrence': occurrence}, layout, sectors)

    def test_anchor_advance_cannot_cross_a_checkpoint(self):
        layout = Layout(course())
        sectors = {p.origin.entity_id: p.sector for p in layout.parts if p.origin}
        anchor = reference('e', 0, 0)
        index, _ = shortcuts.anchor(anchor, layout, sectors)
        advance = layout.sector_starts[1]['point'] - index
        with self.assertRaisesRegex(ValueError, 'remain in its sector'):
            shortcuts.anchor(anchor | {'advance': advance}, layout, sectors)

    def test_cross_sector_backward_overlapping_and_duplicate_cuts_are_rejected(self):
        invalid = [
            specification(shortcut(end='exit')),
            specification(shortcut(start='c', end='a')),
            specification(shortcut('first', 'a', 'd'), shortcut('second', 'b', 'e')),
            specification(shortcut(), shortcut(start='c', end='e')),
            specification(shortcut('normal')),
        ]
        for value in invalid:
            with self.subTest(value=value), self.assertRaises(ValueError):
                shortcuts.compile_shortcuts(value, Layout(course()))

    def test_waypoint_controls_and_landing_require_valid_local_contracts(self):
        invalid = [
            {'waypoints': [reference('exit', 0, 0)]},
            {'waypoints': [reference('a', 0, 0, drive=0)]},
            {'waypoints': [reference('a', 0, 0, hold=1, drive=1)]},
            {'waypoints': [reference('a', 0, 0, hold=True)]},
            {'landing': reference('c', 0, 0, 6)},
            {'landing': reference('exit', 0, 0, 2)},
            {'landing': reference('c', float('nan'), 0, 2)},
            {'landing_radius': 0},
            {'minimum_saved_frames': True},
            {'tradeoff': ' '},
        ]
        for options in invalid:
            with self.subTest(options=options), self.assertRaises(ValueError):
                shortcuts.compile_shortcuts(specification(shortcut() | options), Layout(course()))
        value = shortcut() | {'waypoints': [reference('a', 5, 0, hold=3)]}
        cut = shortcuts.compile_shortcuts(specification(value), Layout(course()))[0]
        self.assertEqual(cut['points'][0][2:], [None, 3])


class ShortcutRunnerTests(unittest.TestCase):
    def run_profiles(self, runtime, failure=None):
        layout = Layout(course())
        cut = shortcuts.compile_shortcuts(specification(shortcut()), layout)[0]
        project.write_json(runtime.specification, specification(shortcut()))
        commands = []

        def execute(command, *, cwd, stdout, **options):
            folder = Path(command[command.index('--write') + 1])
            normal = folder.name == 'normal'
            finish = 600 if normal or failure == 'slow' else 550
            departure, landing = cut['passage']['points']
            rows = []
            for frame in range(370, 410):
                position = departure if frame < 390 else landing
                if frame == 390:
                    position = [(a + b) / 2 for a, b in zip(departure, landing)]
                identity = 8 if failure == 'identity' and frame >= 390 and not normal else 7
                name = 'Ball_Paper' if failure == 'ball' and not normal else 'Ball_Wood'
                rows.append((frame, position, identity, name))
            write_ball_trace(folder, rows)
            (folder / 'physics.csv').write_text('synthetic physics trace')
            if normal or failure != 'checkpoint':
                stdout.write(message(500, 'Checkpoint reached'))
            if not normal and failure == 'death':
                stdout.write(message(450, 'Ball Off'))
            if normal or failure != 'finish':
                stdout.write(message(finish, 'Level_Finish'))
            # Omit just the extra condition; keep checkpoint and finish evidence.
            if normal or failure != 'contract':
                stdout.write(message(480, 'Extrapoint'))
            self.assertEqual(cwd, runtime.game)
            self.assertTrue(options['check'])
            commands.append(command)

        with patch.object(shortcuts.subprocess, 'run', side_effect=execute):
            report = shortcuts.run_shortcuts(
                runtime.source, runtime.specification, runtime.out, runtime.nmo, runtime.game
            )
        return report, commands

    def test_profiles_share_native_inputs_and_save_verifiable_reports(self):
        with synthetic_runtime(shortcuts, course()) as runtime:
            report, commands = self.run_profiles(runtime)
            self.assertEqual(report['status'], 'passed')
            profiles = report['profiles']
            self.assertEqual([p['id'] for p in profiles], ['normal', 'cut', 'combined'])
            self.assertEqual([p['saved_frames'] for p in profiles], [0, 50, 50])
            for command in commands:
                self.assertEqual(command[command.index('--seed') + 1], '1')
                self.assertEqual(command[command.index('-n') + 1], '24000')
                self.assertEqual(command[-1], commands[0][-1])
            records = [
                json.loads((runtime.out / p['id'] / 'input.json').read_text()) for p in profiles
            ]
            self.assertEqual(len({r['level_sha256'] for r in records}), 1)
            self.assertEqual(len({r['harness_sha256'] for r in records}), 1)
            for name in ('shortcuts.json', 'cut/validation.json', 'combined/route_points.json'):
                self.assertEqual(evidence.inspect(runtime.out / name)['status'], 'current')
            (runtime.out / 'cut/validation.json').write_text('{}')
            self.assertEqual(
                evidence.inspect(runtime.out / 'cut/validation.json')['status'], 'stale'
            )

    def test_finish_alone_cannot_hide_missing_contracts_deaths_or_invalid_landings(self):
        for failure in ('checkpoint', 'death', 'finish', 'identity', 'ball', 'slow', 'contract'):
            route = course()
            route['gameplay']['contracts'].append(
                {
                    'id': 'required-action-event',
                    'event': 'Extrapoint',
                    'sector': 1,
                }
            )
            with self.subTest(failure=failure), synthetic_runtime(shortcuts, route) as runtime:
                report, _ = self.run_profiles(runtime, failure)
                self.assertEqual(report['status'], 'failed')
                self.assertEqual(report['profiles'][0]['status'], 'passed')
                self.assertEqual(
                    [p['status'] for p in report['profiles'][1:]], ['failed', 'failed']
                )
                if failure in ('identity', 'ball'):
                    self.assertEqual(report['profiles'][1]['landings'][0]['status'], 'failed')
                if failure == 'contract':
                    self.assertEqual(report['profiles'][1]['gameplay']['status'], 'failed')

    def test_stale_source_replaces_old_success_before_native_execution(self):
        with synthetic_runtime(shortcuts, course()) as runtime:
            runtime.out.mkdir()
            report = runtime.out / 'shortcuts.json'
            project.write_json(report, {'status': 'passed'})
            sidecar = Path(str(report) + '.evidence.json')
            sidecar.write_text('{}')
            (runtime.source / 'route.json').write_text('{}')
            with patch.object(shortcuts.subprocess, 'run') as execute:
                with self.assertRaisesRegex(ValueError, 'stale'):
                    shortcuts.run_shortcuts(
                        runtime.source,
                        runtime.specification,
                        runtime.out,
                        runtime.nmo,
                        runtime.game,
                    )
                execute.assert_not_called()
            self.assertEqual(json.loads(report.read_text())['status'], 'failed')
            self.assertFalse(sidecar.exists())


if __name__ == '__main__':
    unittest.main()
