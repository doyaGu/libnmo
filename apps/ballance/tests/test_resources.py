"""Unified resource provenance and native adapters must agree end to end."""

import copy
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ballance.game.resources import (
    resource,
    identity,
    files,
    digest,
    check_export,
    catalog,
)
from ballance.core import evidence
from ballance.core.nmo import Nmo
from ballance.route.build import build
from ballance.route.layout import Layout
from ballance.route.assets import Assets, MissingMesh
from ballance.verify.resources import check, dependency_paths


class ResourceTests(unittest.TestCase):
    def fixture(self, root):
        out, game = root / 'level', root / 'game'
        out.mkdir()
        game.mkdir()
        spec = resource('Balloon')
        sources = {}
        for name in files(spec):
            path = game / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'native template')
            sources[name] = digest(path)
        assets = {}
        for part in spec['parts']:
            for suffix in ('.obj', '.mtl'):
                filename = part['name'] + suffix
                (out / filename).write_bytes(b'extracted asset')
                assets[filename] = digest(out / filename)
        native = {
            'description_sha256': identity(spec),
            'sources': sources,
            'assets': assets,
            'runtime_bodies': [],
        }
        record = {
            'schema_version': 1,
            'native_assets': True,
            'resources': {'Balloon': {'description': spec, 'native': native}},
            'instances': [],
            'scope': 'fixture',
        }
        (out / 'resources.json').write_text(json.dumps(record))
        return out, game, native

    def test_changed_export_or_description_requires_refresh(self):
        with tempfile.TemporaryDirectory() as tmp:
            out, game, native = self.fixture(Path(tmp))
            self.assertEqual(check(out, game)['status'], 'passed')
            (out / 'PE_Balloon.mtl').write_text('different material')
            self.assertEqual(check(out, game)['status'], 'failed')
            with self.assertRaisesRegex(ValueError, 'asset changed'):
                check_export(out, 'Balloon', native)
            changed = copy.deepcopy(native)
            changed['description_sha256'] = 'old'
            with self.assertRaisesRegex(ValueError, 'description changed'):
                check_export(out, 'Balloon', changed)

    def test_native_template_change_blocks_play_before_packing(self):
        from ballance.verify.play import play, watch

        with tempfile.TemporaryDirectory() as tmp:
            out, game, native = self.fixture(Path(tmp))
            template = game / resource('Balloon')['runtime']['template']
            template.write_bytes(b'changed behavior')
            report = check(out, game)
            self.assertEqual(report['status'], 'failed')
            self.assertEqual(
                report['findings'][0]['file'], resource('Balloon')['runtime']['template']
            )
            with (
                patch('ballance.verify.play.pack') as pack,
                self.assertRaisesRegex(ValueError, 'resources changed'),
            ):
                play(out, 1, Nmo('absent-nmo'), game)
            pack.assert_not_called()
            with (
                patch('ballance.verify.play.harness.ensure') as harness,
                self.assertRaisesRegex(ValueError, 'resources changed'),
            ):
                watch(out / 'Level_01.NMO', None, Nmo('absent-nmo'), game)
            harness.assert_not_called()
            template.unlink()
            self.assertEqual(check(out, game)['status'], 'failed')

    def test_resource_dependencies_invalidate_evidence_and_missing_provenance_is_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out, game, _ = self.fixture(root)
            source = root / 'source.nmo'
            source.write_bytes(b'level')
            artifact = root / 'report.json'
            artifact.write_text('{}')
            record = evidence.capture(source, 'resource-test', {}, dependency_paths(out, game))
            evidence.attach(artifact, record)
            self.assertEqual(evidence.inspect(artifact)['status'], 'current')
            (game / resource('Balloon')['runtime']['template']).write_bytes(b'new template')
            self.assertEqual(evidence.inspect(artifact)['status'], 'stale')
            (out / 'resources.json').unlink()
            self.assertEqual(check(out, game)['status'], 'unavailable')

    def test_native_build_requires_a_resource_extraction_record(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out = root / 'out'
            out.mkdir()
            elements = root / 'elements'
            elements.mkdir()
            with self.assertRaisesRegex(MissingMesh, 'resource_index'):
                Assets(out, [], [], elements)

    def test_native_port_frames_match_generated_tower_and_finish_for_all_headings(self):
        for heading in ('+x', '-x', '+z', '-z'):
            route = {
                'start': {'heading': heading},
                'scenery': {'omit_supports': ['FanTower_Pillar']},
                'sectors': [
                    [
                        {'road': 10},
                        {'trafo': 'paper'},
                        {'road': 10},
                        {'rail': 15},
                        {'tower': True},
                        {'road': 15},
                        {'trafo': 'wood'},
                        {'road': 15},
                    ]
                ],
            }
            with tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp)
                build(route, out, None, 'Frames')
                layout = Layout(route)
                record = json.loads((out / 'resources.json').read_text())
                tower = next(p for p in layout.parts if p.what == 'tower')
                frame = next(i for i in record['instances'] if i['resource'] == 'FanTower')
                exit = frame['ports']['exit']
                (x, z), h, d = tower.end
                self.assertLess(
                    math.dist(exit['position'], (x - 5 * d[0], h + 0.7, z - 5 * d[1])), 1e-6
                )
                self.assertLess(math.dist(exit['direction'], (d[0], 0, d[1])), 1e-6)
                end = layout.parts[-1]
                (x, z), h, d = end.end
                finish = next(i for i in record['instances'] if i['resource'] == 'Balloon')[
                    'ports'
                ]['entry']
                self.assertLess(
                    math.dist(finish['position'], (x + 5 * d[0], h + 0.7, z + 5 * d[1])), 1e-6
                )
                self.assertEqual(check(out)['status'], 'review')

    @unittest.skipUnless(
        os.environ.get('BALLANCE')
        and os.environ.get('BALLANCE_ELEMENTS')
        and os.environ.get('NMO_TEST_CLI'),
        'requires local native sources and extracted elements',
    )
    def test_native_resource_extraction_matches_existing_assets_and_records_runtime_meshes(
        self,
    ):
        from ballance.extract.elements import Levels
        from ballance.extract.resources import export_resources
        from ballance.game.resources import INDEX

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            found = export_resources(
                Levels(Path(os.environ['BALLANCE']), Nmo(os.environ['NMO_TEST_CLI'])), out
            )
            self.assertEqual(
                set(found), {p['name'] for spec in catalog().values() for p in spec['parts']}
            )
            record = json.loads((out / INDEX).read_text())
            for name, item in record['resources'].items():
                check_export(out, name, item)
                # Start/checkpoint flame templates have no mesh-bearing entities.
                self.assertEqual(
                    bool(item['runtime_bodies']),
                    'runtime' in resource(name)
                    and name not in ('StartFlames', 'CheckpointFlames'),
                    name,
                )
                for filename, sha in item['assets'].items():
                    self.assertEqual(
                        sha, digest(Path(os.environ['BALLANCE_ELEMENTS']) / filename), filename
                    )
            self.assertTrue(
                any(
                    b['name'].startswith('PE_Balloon')
                    for b in record['resources']['Balloon']['runtime_bodies']
                )
            )

    def test_authored_scenery_frames_and_invalid_ownership(self):
        from ballance.route.attachments import authored
        from ballance.core.geometry import at

        for heading in ('+x', '-x', '+z', '-z'):
            layout = Layout(
                {
                    'schema_version': 1,
                    'sectors': [[{'road': 30, 'id': 'road'}]],
                    'start': {'heading': heading},
                }
            )
            parent = next(p for p in layout.parts if p.origin)
            value = {
                'id': 'light',
                'resource': 'LanternTower',
                'parent': parent.origin.entity_id,
                'at': [-5, 20, -4.3],
            }
            layout.resource_scenery = [value]
            parts = {parent.origin.entity_id: parent}
            actor = authored(layout, parts)[0]
            p, h, d = parent.end
            x, z = at(p, d, -5, 20)
            self.assertEqual(actor['anchor'], [x, h - 4.3, z])
            self.assertEqual(actor['kinds'], ('Tower', 'Pillar'))
            for change in (
                {'parent': 'missing'},
                {'resource': 'FinishPole'},
                {'at': [0, float('nan'), 0]},
            ):
                layout.resource_scenery = [value | change]
                with self.assertRaises(ValueError):
                    authored(layout, parts)
            layout.resource_scenery = [value, value]
            with self.assertRaisesRegex(ValueError, 'unique'):
                authored(layout, parts)

    @unittest.skipUnless(
        os.environ.get('BALLANCE_ELEMENTS') and os.environ.get('BALLANCE'),
        'requires native elements',
    )
    def test_native_resource_assemblies_and_attachment_failures(self):
        elements = Path(os.environ['BALLANCE_ELEMENTS'])
        game = Path(os.environ['BALLANCE'])
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'level'
            route = {
                'schema_version': 1,
                'sectors': [
                    [
                        {'road': 20, 'id': 'road'},
                        {'resource': 'Drawbridge', 'id': 'bridge'},
                        {'road': 15},
                    ]
                ],
                'scenery': {
                    'finish_poles': [],
                    'instances': [
                        {
                            'id': 'light',
                            'resource': 'LanternTower',
                            'parent': 'route/id/road',
                            'at': [-10, 25, -4.3],
                        }
                    ],
                },
            }
            build(route, out, elements, 'Resource Assemblies')
            self.assertEqual(check(out, game)['status'], 'passed')
            record = json.loads((out / 'resources.json').read_text())
            bridge = next(i for i in record['instances'] if i['resource'] == 'Drawbridge')
            self.assertEqual(bridge['variant'], 'standalone')
            self.assertEqual(len(bridge['components']), 3)
            self.assertTrue(all(c['status'] == 'placed' for c in bridge['components']))
            light = next(i for i in record['instances'] if i['id'] == 'scenery/id/light')
            self.assertEqual(len({o for c in light['components'] for o in c['objects']}), 2)
            route['scenery']['instances'][0] |= {'resource': 'Column', 'at': [-10, 0, -4.3]}
            build(route, out, elements, 'Attached Column')
            self.assertEqual(check(out, game)['status'], 'passed')
            # A column may not be silently detached from its owning road.
            route['scenery']['instances'][0] |= {'resource': 'Column', 'at': [-10, 25, -4.3]}
            route['scenery']['rules'] = {'scenery/id/light': {'max_shift': 0}}
            with self.assertRaisesRegex(ValueError, 'cannot satisfy'):
                build(route, out, elements, 'Detached Column')
            # A locked full lantern placed in the road must fail clearance.
            route['scenery']['instances'][0] |= {'resource': 'LanternTower', 'at': [-10, 0, 0]}
            route['scenery']['rules'] = {'scenery/id/light': {'mode': 'locked'}}
            with self.assertRaisesRegex(ValueError, 'cannot satisfy'):
                build(route, out, elements, 'Obstructed Lantern')


if __name__ == '__main__':
    unittest.main()
