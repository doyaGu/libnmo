"""Generation isolation, publication rollback and evidence freshness."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core import evidence, project
from ballance.route.build import build


class ProjectTests(unittest.TestCase):
    route = {'schema_version': 1, 'sectors': [[{'id': 'entry', 'road': 20}]]}

    def test_failed_mesh_write_keeps_old_inputs_and_invalidates_old_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp) / 'level'
            build(self.route, folder, None, 'Before')
            before = {p: project.digest(folder / p) for p in project.inputs(folder)}
            (folder / 'validation.json').write_text('{"status":"passed"}')

            def fail(sector, index, staged):
                (staged / 'sector1.obj').write_text('incomplete')
                raise OSError('injected write failure')

            with patch('ballance.route.build.write_sector', side_effect=fail):
                with self.assertRaisesRegex(OSError, 'injected'):
                    build(self.route, folder, None, 'After')
            self.assertEqual({p: project.digest(folder / p) for p in before}, before)
            self.assertEqual(project.inspect(folder)['status'], 'failed')
            self.assertEqual(
                json.loads((folder / 'validation.json').read_text())['status'], 'superseded'
            )
            self.assertFalse(list(Path(tmp).glob('.level.build-*')))
            build(self.route, folder, None, 'Recovered')
            self.assertEqual(project.inspect(folder)['status'], 'passed')

    def test_rebuild_archives_previous_evidence_without_leaking_it_into_current_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp) / 'level'
            build(self.route, folder, None, 'Before')
            (folder / 'panorama.png').write_bytes(b'previous picture')
            (folder / 'steer.csv').write_text('previous trajectory')
            previous_id = project.inspect(folder)['build_id']
            build(self.route, folder, None, 'After')
            self.assertFalse((folder / 'panorama.png').exists())
            self.assertFalse((folder / 'steer.csv').exists())
            history = next((folder / '.history').glob('*/panorama.png')).parent
            self.assertEqual((history / 'panorama.png').read_bytes(), b'previous picture')
            self.assertEqual(
                json.loads((history / project.BUILD_RECORD).read_text())['build_id'], previous_id
            )
            # A further rebuild retains flat history, including the oldest image.
            build(self.route, folder, None, 'Third')
            self.assertTrue((history / 'panorama.png').is_file())
            self.assertFalse(list((folder / '.history').glob('*/.history')))

    def test_failed_publication_restores_previous_directory_and_history(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp) / 'level'
            build(self.route, folder, None, 'Before')
            (folder / 'keep.txt').write_text('previous generation')
            before = (folder / 'manifest.json').read_bytes()
            history = sorted(p.name for p in (folder / '.history').iterdir())
            rename = Path.rename

            def fail_publish(path, target):
                if path.name.startswith('.level.build-') and target == folder:
                    raise OSError('injected publish failure')
                return rename(path, target)

            with patch.object(Path, 'rename', fail_publish):
                with self.assertRaisesRegex(OSError, 'publish'):
                    build(self.route, folder, None, 'After')
            self.assertEqual((folder / 'manifest.json').read_bytes(), before)
            self.assertEqual((folder / 'keep.txt').read_text(), 'previous generation')
            self.assertEqual(sorted(p.name for p in (folder / '.history').iterdir()), history)
            self.assertEqual(project.inspect(folder)['status'], 'failed')

    def test_missing_asset_and_locked_build_do_not_publish_partial_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder, assets = Path(tmp) / 'level', Path(tmp) / 'assets'
            assets.mkdir()
            build(self.route, folder, None, 'Before')
            before = (folder / 'manifest.json').read_bytes()
            self.assertEqual(build(self.route, folder, assets, 'After'), 2)
            self.assertEqual((folder / 'manifest.json').read_bytes(), before)
            lock = folder.parent / '.level.build-lock'
            lock.mkdir()
            with self.assertRaisesRegex(ValueError, 'build lock'):
                build(self.route, folder, None, 'Locked')
            self.assertTrue(lock.is_dir())

    def test_invalid_source_json_invalidates_success_without_overwriting_geometry(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder, source = Path(tmp) / 'level', Path(tmp) / 'source.json'
            build(self.route, folder, None, 'Before')
            before = (folder / 'sector1.obj').read_bytes()
            (folder / 'validation.json').write_text('{"status":"passed"}')
            source.write_text('{invalid JSON')
            with self.assertRaises(ValueError):
                build(source, folder, None, 'After')
            self.assertEqual((folder / 'sector1.obj').read_bytes(), before)
            self.assertEqual(
                json.loads((folder / 'validation.json').read_text())['status'], 'superseded'
            )

    def test_failed_rollback_retains_recovery_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp) / 'level'
            build(self.route, folder, None, 'Before')
            old_manifest = (folder / 'manifest.json').read_bytes()
            rename = Path.rename

            def fail_destination(path, target):
                if target == folder:
                    raise OSError('injected destination failure')
                return rename(path, target)

            with patch.object(Path, 'rename', fail_destination):
                with self.assertRaisesRegex(RuntimeError, 'recovery files retained'):
                    build(self.route, folder, None, 'After')
            recovery = next(Path(tmp).glob('.level.build-*'))
            archived = list((recovery / '.history').glob('*/manifest.json'))
            self.assertTrue(any(path.read_bytes() == old_manifest for path in archived))

    def test_external_preview_tracks_build_tools_dependencies_and_its_own_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            folder, artifact, texture = root / 'level', root / 'view.png', root / 'tile.bmp'
            build(self.route, folder, None, 'Before')
            texture.write_bytes(b'old texture')
            artifact.write_bytes(b'preview')
            record = evidence.capture(folder, 'map-preview', {'yaw': 35}, [texture])
            evidence.attach(artifact, record)
            self.assertEqual(evidence.inspect(artifact)['status'], 'current')
            texture.write_bytes(b'new texture')
            self.assertEqual(evidence.inspect(artifact)['reasons'], ['dependencies changed.'])
            texture.write_bytes(b'old texture')
            artifact.write_bytes(b'changed preview')
            self.assertIn('Artifact content changed.', evidence.inspect(artifact)['reasons'])
            artifact.write_bytes(b'preview')
            actual = project.tools_identity()
            with patch(
                'ballance.core.project.tools_identity', return_value=actual | {'changed.py': 'x'}
            ):
                self.assertEqual(project.inspect(folder)['tool_changes'], ['changed.py'])
                self.assertEqual(evidence.inspect(artifact)['status'], 'stale')
            build(self.route, folder, None, 'After')
            self.assertEqual(evidence.inspect(artifact)['status'], 'stale')

    def test_build_record_tampering_and_missing_artifacts_are_not_current(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp) / 'level'
            build(self.route, folder, None, 'Probe')
            path = folder / project.BUILD_RECORD
            record = json.loads(path.read_text())
            record['warnings'] = ['changed']
            path.write_text(json.dumps(record))
            self.assertIn(project.BUILD_RECORD, project.inspect(folder)['changed'])
            self.assertEqual(evidence.inspect(folder / 'missing.png')['status'], 'unavailable')


if __name__ == '__main__':
    unittest.main()
