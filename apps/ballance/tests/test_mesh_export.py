"""CLI mesh export regressions using a generated, redistributable fixture."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

NMO = Path(
    os.environ.get('NMO_TEST_CLI')
    or Path(__file__).resolve().parents[3]
    / 'build/tools'
    / ('nmo.exe' if os.name == 'nt' else 'nmo')
)


@unittest.skipUnless(NMO.is_file(), 'build/tools/nmo is required')
class MeshExportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        (self.path / 'mesh.obj').write_text('v 0 0 0\nv 1 0 0\nv 0 0 1\nusemtl Stone\nf 1 2 3\n')
        manifest = {
            'version': 1,
            'document': {'name': 'Test'},
            'materials': [{'name': 'Stone', 'diffuse': [1, 1, 1, 1]}],
            'scenes': [
                {
                    'name': 'Test',
                    'objects': [
                        {'name': name, 'class': 'CK3dObject', 'mesh': {'obj': 'mesh.obj'}}
                        for name in ('First', 'Second')
                    ],
                }
            ],
        }
        (self.path / 'manifest.json').write_text(json.dumps(manifest))
        self.source = self.path / 'test.nmo'
        self.run_nmo('patch', 'apply', '--project', self.path / 'manifest.json', '-o', self.source)
        data = json.loads(self.run_nmo('-f', 'json', 'mesh', 'list', self.source).stdout)
        self.meshes = {m['name']: m['id'] for m in data['data']['meshes']}

    def run_nmo(self, *args, check=True):
        command = (
            [str(NMO), *(os.fspath(a) for a in args)]
            if os.name == 'nt'
            else [os.fsencode(NMO), *(os.fsencode(a) for a in args)]
        )
        result = subprocess.run(command, capture_output=True)
        if check:
            self.assertEqual(result.returncode, 0, result.stderr)
        return result

    @unittest.skipIf(os.name == 'nt', 'raw legacy command-line bytes require POSIX')
    def test_legacy_mesh_names_export_as_utf8(self):
        mesh_id = self.meshes['First_Mesh']
        renamed = self.path / 'renamed.nmo'
        self.run_nmo('object', 'rename', str(mesh_id), b'Gel\xe4nder', self.source, '-o', renamed)
        output = self.path / 'export'
        result = self.run_nmo('mesh', 'export', '--id', str(mesh_id), '-d', output, renamed)
        result.stdout.decode('utf-8')
        result.stderr.decode('utf-8')
        obj = output / f'Geländer_{mesh_id}.obj'
        mtl = output / f'Geländer_{mesh_id}.mtl'
        self.assertTrue(obj.is_file())
        self.assertTrue(mtl.is_file())
        self.assertIn(f'mtllib {mtl.name}', obj.read_text(encoding='utf-8'))
        mtl.read_text(encoding='utf-8')

    @unittest.skipIf(os.name == 'nt', 'raw legacy command-line bytes require POSIX')
    def test_legacy_material_and_utf8_mesh_names(self):
        mesh_id = self.meshes['First_Mesh']
        mesh = json.loads(
            self.run_nmo('-f', 'json', 'mesh', 'show', '--id', str(mesh_id), self.source).stdout
        )['data']
        material_id = mesh['material_groups'][0]['material_id']
        self.assertNotEqual(material_id, 0)
        material_renamed = self.path / 'material.nmo'
        renamed = self.path / 'renamed.nmo'
        self.run_nmo(
            'object',
            'rename',
            str(material_id),
            b'M\xe4terial',
            self.source,
            '-o',
            material_renamed,
        )
        self.run_nmo('object', 'rename', str(mesh_id), '道路', material_renamed, '-o', renamed)
        output = self.path / 'export'
        self.run_nmo('mesh', 'export', '--id', str(mesh_id), '-d', output, renamed)
        obj = (output / f'道路_{mesh_id}.obj').read_text(encoding='utf-8')
        mtl = (output / f'道路_{mesh_id}.mtl').read_text(encoding='utf-8')
        self.assertIn('usemtl Mäterial', obj)
        self.assertIn('newmtl Mäterial', mtl)

    def test_partial_export_failure_returns_error(self):
        output = self.path / 'export'
        output.mkdir()
        (output / f'First_Mesh_{self.meshes["First_Mesh"]}.mtl').mkdir()
        result = self.run_nmo('mesh', 'export', '--all', '-d', output, self.source, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((output / f'Second_Mesh_{self.meshes["Second_Mesh"]}.obj').is_file())
        self.assertIn(b'Errors: 1', result.stdout)
        result = self.run_nmo(
            '-f', 'json', 'mesh', 'export', '--all', '-d', output, self.source, check=False
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stdout)['data']['errors'], 1)


if __name__ == '__main__':
    unittest.main()
