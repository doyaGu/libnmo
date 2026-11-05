from pathlib import Path
import json
from hashlib import sha256
import os
import tempfile
import unittest
from unittest.mock import patch
import support  # noqa: F401 - resolve the external toolchain before application imports
from ballance_editor.native import NativePreview, frame, multiply, IDENTITY
from ballance.core.nmo import Nmo, place
from ballance.game.resources import catalog


class NativePreviewTests(unittest.TestCase):
    def test_instance_frame_preserves_template_offsets(self):
        m = multiply(frame([10, 20, 30], 90), frame([2, 3, 4], 0))
        for got, want in zip(place(m, (1, 0, 0)), (14, 23, 27)):
            self.assertAlmostEqual(got, want)
        self.assertEqual(multiply(IDENTITY, m), m)

    def test_recorded_poses_preserve_gaps_and_reject_stale_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            p = NativePreview(None, None, root / 'cache')
            report = {
                'checks': {
                    'mechanism_motion': {
                        'bodies': [
                            {
                                'mechanism': 'a',
                                'body': 'bridge',
                                'runtime_ids': ['5'],
                                'active_ranges': [[1, 3], [20, 22]],
                                'first_displaced_frame': 2,
                            }
                        ]
                    },
                    'play': {'finished_frame': 22},
                },
                'evidence': {'parameters': {'frame_ms': '16.667'}},
            }
            (root / 'full-validation.json').write_text(json.dumps(report))
            rows = ['frame,id,name,' + ','.join('m' + str(i) for i in range(16))]
            for f in [1, 2, 3, 20, 21, 22]:
                rows.append(f'{f},5,bridge,' + ','.join(map(str, frame([f, 0, 0], 0))))
            (root / 'physics.csv').write_text('\n'.join(rows))
            report['checks']['mechanism_motion']['trace_sha256'] = sha256(
                (root / 'physics.csv').read_bytes()
            ).hexdigest()
            (root / 'full-validation.json').write_text(json.dumps(report))
            with patch(
                'ballance_editor.native.evidence.inspect', return_value={'status': 'current'}
            ):
                result = p.motion(root)
                t = result['tracks']['a/bridge']
                self.assertEqual([x[0] for x in t['samples']], [1, 3, 20, 22])
                self.assertEqual(t['active_ranges'], [[1, 3], [20, 22]])
                (root / 'physics.csv').write_text((root / 'physics.csv').read_text() + '\n')
                self.assertFalse(p.motion(root)['available'])
            with patch('ballance_editor.native.evidence.inspect', return_value={'status': 'stale'}):
                self.assertFalse(p.motion(root)['available'])

    @unittest.skipUnless(
        os.environ.get('BALLANCE') and os.environ.get('NMO_TEST_CLI'), 'native game assets required'
    )
    def test_scene_replaces_markers_without_removing_native_floor_components(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            spec = catalog()['Drawbridge']
            instance = {
                'id': 'test/mechanism',
                'parent': 'test',
                'resource': 'Drawbridge',
                'sector': 2,
                'position': [50, 20, -30],
                'yaw': 90,
                'components': [
                    {'role': 'placement_marker', 'objects': ['BridgeMarker']},
                    {'role': 'surface', 'objects': ['BridgeFloor']},
                ],
            }
            (root / 'resources.json').write_text(
                json.dumps(
                    {'resources': {'Drawbridge': {'description': spec}}, 'instances': [instance]}
                )
            )
            preview = NativePreview(
                Path(os.environ['BALLANCE']), Nmo(os.environ['NMO_TEST_CLI']), root / 'cache'
            )
            objects, materials, replaced, instances = preview.scene(root)
            self.assertEqual(replaced, {'BridgeMarker'})
            self.assertEqual(
                {o['body'] for o in objects}, {'P_Modul_25_Bridge', 'P_Modul_25_Hinge'}
            )
            names = {m['name'] for m in materials}
            for o in objects:
                self.assertEqual(
                    o['matrix'], multiply(frame(instance['position'], 90), o['template_matrix'])
                )
                self.assertTrue(all(t['material'] in names for t in o['triangles']))
                self.assertEqual(o['sector'], 2)
            self.assertEqual(sum(bool(o['joint']) for o in objects), 1)

    @unittest.skipUnless(
        os.environ.get('BALLANCE') and os.environ.get('NMO_TEST_CLI'), 'native game assets required'
    )
    def test_all_native_mechanism_templates_have_render_meshes_and_materials(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = NativePreview(
                Path(os.environ['BALLANCE']), Nmo(os.environ['NMO_TEST_CLI']), Path(tmp)
            )
            templates = {
                s['runtime']['template']
                for s in catalog().values()
                if s.get('category') == 'mechanism'
            }
            self.assertGreaterEqual(len(templates), 13)
            for template in templates:
                with self.subTest(template=template):
                    data = p.template(template)
                    self.assertTrue(data['objects'])
                    materials = {m['name'] for m in data['materials']}
                    for obj in data['objects']:
                        self.assertEqual(len(obj['matrix']), 16)
                        self.assertTrue(
                            all(
                                t['material'] in materials and t['material'] != 'material_0'
                                for t in obj['triangles']
                            )
                        )
                        self.assertNotIn('Kollisions', obj['body'])
            balloon = p.template('3D Entities/PH/PE_Balloon.nmo')
            self.assertFalse(any(o['body'].startswith('PE_UFO') for o in balloon['objects']))
            fan = p.template('3D Entities/PH/P_Modul_18.nmo')
            self.assertEqual([o['body'] for o in fan['objects']], ['P_Modul_18_Rotor'])


if __name__ == '__main__':
    unittest.main()
