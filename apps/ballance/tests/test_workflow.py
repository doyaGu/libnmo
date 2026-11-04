"""Build identity, exported clearance checks, gameplay evidence and preview control."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core import project
from ballance.core.nmo import Nmo
from ballance.core.obj import write_box
from ballance.kit.rails import Ring
from ballance.route.build import build
from ballance.route.layout import Layout, Part
from ballance.verify.play import play, read_run, watch
from ballance.verify.geometry import Finding
from ballance.verify.report import verify, write
from ballance.verify.scenery import check as check_scenery
from ballance.view.scene import load_project


class WorkflowTests(unittest.TestCase):
    def test_authored_support_omission_keeps_the_fan_tower_housing(self):
        course = {'scenery': {'omit_supports': ['FanTower_Pillar']},
                  'sectors': [[{'road': 10}, {'trafo': 'paper'}, {'road': 10},
                               {'rail': 15}, {'tower': True}, {'road': 15},
                               {'trafo': 'wood'}, {'road': 15}]]}
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            self.assertEqual(build(course, folder, None, 'Fan'), 0)
            names = [o['name'] for o in json.loads((folder / 'manifest.json').read_text())['scenes'][0]['objects']]
            self.assertTrue(any('_Floor_FanTower_' in n for n in names))
            self.assertFalse(any('_FanTower_Pillar_' in n for n in names))
            placements = json.loads((folder / 'scenery_placement.json').read_text())
            self.assertEqual(placements[0]['action'], 'omitted by design')

    def test_departure_checks_columns_ahead_of_the_parked_balloon(self):
        layout = Layout({'sectors': [[{'road': 10}]]})
        layout.parts = []
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            write_box(folder / 'column.obj', (-1, 1, -100, 30, -1, 1))
            obj = {'name': 'A04_FanTower_Pillar_01', 'mesh': {'obj': 'column.obj'},
                   'transform': {'position': [40, 0, 0]}}
            def findings():
                (folder / 'manifest.json').write_text(json.dumps({'scenes': [{'objects': [obj]}]}))
                return check_scenery(load_project(folder), layout)
            for heading, position in [((1, 0), [40, 0, 0]), ((0, -1), [0, 0, -40])]:
                layout.balloon = ([0, 0, 0], heading)
                obj['transform']['position'] = position
                self.assertEqual([f['kind'] for f in findings()], ['balloon departure'])
            # A pole beside the balloon and a support wholly above it
            # must remain valid, including when the finish is rotated.
            obj['transform']['position'] = [24, 0, 0]
            self.assertEqual(findings(), [])
            obj['transform']['position'] = [0, 150, -40]
            self.assertEqual(findings(), [])

    def test_input_error_replaces_old_success_and_returns_machine_readable_failure(self):
        script = Path(__file__).resolve().parents[1] / 'scripts/ballance_verify_level.py'
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / 'manifest.json').write_text('broken JSON')
            output = folder / 'validation.json'
            output.write_text('{"status":"passed"}')
            result = subprocess.run([sys.executable, str(script), str(folder), '--json'],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(json.loads(result.stdout)['status'], 'failed')
            self.assertEqual(json.loads(output.read_text()), json.loads(result.stdout))

    def test_geometry_review_remains_visible_and_strict_mode_fails_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            build({'sectors': [[{'road': 20}]]}, folder, None, 'Probe')
            finding = Finding('hole', (1, 2, 3), 'a known pocket')
            with patch('ballance.verify.report.geometry_check', return_value=[finding]):
                report = verify(folder, Nmo())
                strict = verify(folder, Nmo(), strict=True)
            self.assertEqual(report['checks']['geometry']['status'], 'review')
            self.assertEqual(report['checks']['geometry']['findings'][0]['what'], 'a known pocket')
            self.assertEqual(strict['status'], 'failed')
            self.assertEqual(report['checks']['play']['status'], 'skipped')

    def test_rebuild_invalidates_old_validation_and_resets_scenery_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / 'validation.json').write_text('{"status":"passed"}')
            (folder / 'scenery_placement.json').write_text('[{"action":"omitted"}]')
            build({'sectors': [[{'road': 20}]]}, folder, None, 'Probe')
            self.assertFalse((folder / 'validation.json').exists())
            self.assertEqual(json.loads((folder / 'scenery_placement.json').read_text()), [])

    def test_build_records_only_current_inputs_and_detects_mesh_edits_and_removal(self):
        route = {'sectors': [[{'road': 20}]]}
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / 'obsolete.obj').write_text('not part of this map')
            self.assertEqual(build(route, folder, None, 'Probe'), 0)
            self.assertEqual(json.loads((folder / 'route.json').read_text()), route)
            self.assertEqual(project.inspect(folder)['status'], 'passed')
            self.assertNotIn('obsolete.obj', project.inputs(folder))
            mesh = folder / 'sector1.obj'
            mesh.write_text(mesh.read_text() + '# edited\n')
            self.assertEqual(project.inspect(folder)['changed'], ['sector1.obj'])
            mesh.unlink()
            self.assertEqual(project.inspect(folder)['missing'], ['sector1.obj'])

    def test_recorded_side_file_cannot_silently_disappear(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            build({'sectors': [[{'road': 20}]]}, folder, None, 'Probe')
            (folder / 'route.json').unlink()
            self.assertEqual(project.inspect(folder)['missing'], ['route.json'])

    def test_changed_build_is_reported_without_running_old_gameplay(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            build({'sectors': [[{'road': 20}]]}, folder, None, 'Probe')
            mesh = folder / 'sector1.obj'
            mesh.write_text(mesh.read_text() + '# changed\n')
            with patch('ballance.verify.report.play') as run:
                report = verify(folder, Nmo(), frames=1000, game=folder)
            run.assert_not_called()
            self.assertEqual(report['status'], 'failed')
            self.assertEqual(report['checks']['play']['status'], 'skipped')
            write(report, folder / 'validation.json')
            self.assertEqual(json.loads((folder / 'validation.json').read_text()), report)

    def test_exported_column_and_ball_headroom_are_checked_in_world_space(self):
        layout = Layout({'sectors': [[{'road': 10}]]})
        layout.parts = [Part(0, 1, 'beam', segments=[
            (Ring(10, 0, -10, 0, 1, gauge=0), Ring(10, 0, 10, 0, 1, gauge=0))])]
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            # A horizontal capital at y=3, well above the tube but inside
            # the radius-two ball's space. Rotation places it across x=10.
            write_box(folder / 'column.obj', (-1, 1, 3, 4, -5, 5))
            obj = {'name': 'Floor_Columns_01', 'mesh': {'obj': 'column.obj'},
                   'transform': {'position': [10, 0, 0], 'rotation_euler_deg': [0, 90, 0]}}
            def findings():
                (folder / 'manifest.json').write_text(json.dumps({'scenes': [{'objects': [obj]}]}))
                return check_scenery(load_project(folder), layout)
            self.assertEqual(findings()[0]['kind'], 'ball clearance')
            obj['transform']['position'][1] = -3
            self.assertEqual(findings()[0]['kind'], 'rail')
            obj['transform']['position'][1] = 10
            self.assertEqual(findings(), [])

    def test_separate_braces_do_not_fill_the_empty_space_between_them(self):
        from ballance.core.obj import write_boxes
        layout = Layout({'sectors': [[{'road': 10}]]})
        layout.parts = [Part(0, 1, 'beam', segments=[
            (Ring(0, 0, -10, 0, 1, gauge=0), Ring(0, 0, 10, 0, 1, gauge=0))])]
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            write_boxes(folder / 'braces.obj', [(-12, -10, -5, 5, -2, 2), (10, 12, -5, 5, -2, 2)])
            (folder / 'manifest.json').write_text(json.dumps({'scenes': [{'objects': [
                {'name': 'Support_Scenery', 'mesh': {'obj': 'braces.obj'}}]}]}))
            self.assertEqual(check_scenery(load_project(folder), layout), [])

    def test_relative_executable_survives_gameplay_working_directory(self):
        with tempfile.TemporaryDirectory(dir='.') as tmp:
            path = Path(tmp) / 'nmo'
            path.write_text('#!/bin/sh\nexit 0\n')
            path.chmod(0o755)
            nmo = Nmo('./' + os.path.relpath(path))
            self.assertEqual(nmo.executable, str(path.resolve()))

    def test_failed_gameplay_removes_previous_trace_and_reports_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            build({'sectors': [[{'road': 10}]]}, folder, None, 'Failed play probe')
            (folder / 'steer.csv').write_text('old successful trace')
            with patch('ballance.verify.play.pack'), patch('ballance.verify.play.base_for', return_value=folder/'base.nmo'), \
                    patch('ballance.verify.play.subprocess.run', return_value=subprocess.CompletedProcess([], 7)):
                with self.assertRaisesRegex(RuntimeError, 'status 7'):
                    play(folder, 1000, Nmo(), folder)
            self.assertFalse((folder / 'steer.csv').exists())

    def test_summary_ignores_events_and_positions_after_finish(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / 'route_points.json').write_text('[[0,0],[10,0]]')
            (folder / 'manifest.json').write_text(json.dumps({'scenes': [{'objects': [
                {'name': 'P_Extra_Point_01', 'transform': {'position': [10, 0, 0]}},
                {'name': 'P_Extra_Point_02', 'transform': {'position': [100, 0, 0]}}]}]}))
            (folder / 'run.log').write_text('\n'.join(
                f'[{frame}] Send Message#1 sends "{event}" to All_Gameplay'
                for frame, event in [(400, 'Checkpoint reached'), (500, 'Level_Finish'), (600, 'Ball Off')]))
            (folder / 'steer.csv').write_text('frame,x,y,z,speed,next,up,down,left,right\n'
                '360,0,0,0,0,0,0,0,0,0\n500,10,0,0,1,1,0,0,0,0\n600,100,-500,0,0,1,0,0,0,0\n')
            result = read_run(folder, 1000)
            self.assertEqual(result.finished, 500)
            self.assertEqual(result.falls, [])
            self.assertEqual(len(result.trace), 2)
            self.assertEqual([r['reached'] for r in result.rewards], [True, False])
            self.assertEqual(result.missed[0][2], 90)
            (folder / 'steer.csv').write_text('frame,x,y,z,speed,next\n')
            with self.assertRaisesRegex(RuntimeError, 'empty trace'):
                read_run(folder, 1000)

    def test_preview_hands_over_and_keeps_screenshot_path_outside_game_cwd(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            level = folder / 'Level_01.NMO'
            level.write_bytes(b'fixture')
            route = folder / 'route_points.json'
            route.write_text('[[0,0],[10,0],[20,0]]')
            with patch('ballance.verify.play.harness.ensure', return_value=folder/'base.nmo'), \
                    patch('ballance.verify.play.subprocess.run', return_value=subprocess.CompletedProcess([], 0)) as run:
                self.assertEqual(watch(level, route, Nmo(), folder, handoff_frame=1000,
                                       approach=True, shot=folder/'preview.png', shot_frame=1100), 0)
            command = run.call_args.args[0]
            self.assertEqual(command[command.index('--steer')+1], '0,0;10,0@360-1000')
            self.assertEqual(command[command.index('--shot')+1], f'{folder.resolve()}/preview.png@1100')
            with self.assertRaisesRegex(ValueError, 'handoff'):
                watch(level, route, Nmo(), folder, approach=True)


if __name__ == '__main__':
    unittest.main()
