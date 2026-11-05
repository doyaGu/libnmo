from io import BytesIO
from pathlib import Path
import json
import tempfile
import unittest
from PIL import Image
import support  # noqa: F401 - resolve the external toolchain before application imports
from ballance_editor.materials import Materials


class MaterialTests(unittest.TestCase):
    def test_original_color_alpha_and_case_insensitive_texture_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'Textures').mkdir()
            project = root / 'project'
            project.mkdir()
            image = Image.new('RGBA', (2, 1))
            image.putdata([(123, 45, 67, 0), (10, 20, 30, 128)])
            image.save(root / 'Textures' / 'Brick.tga')
            (project / 'manifest.json').write_text(
                json.dumps(
                    {
                        'materials': [
                            {'name': 'Brick', 'texture': 'brick.TGA', 'diffuse': [1, 1, 1, 1]}
                        ]
                    }
                )
            )
            materials = Materials(root)
            entry = materials.catalog(project)[0]
            self.assertFalse(entry['texture_missing'])
            digest = entry['texture_url'].split('/')[-1][:-4]
            loaded = Image.open(BytesIO(materials.image(digest)))
            self.assertEqual(list(loaded.getdata()), [(123, 45, 67, 0), (10, 20, 30, 128)])
            self.assertEqual(entry['diffuse'], [1, 1, 1, 1])

    def test_missing_and_outside_textures_never_become_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'Textures').mkdir()
            (root / 'manifest.json').write_text(
                json.dumps(
                    {
                        'materials': [
                            {'name': 'outside', 'texture': '../private.png'},
                            {'name': 'missing', 'texture': 'none.bmp'},
                        ]
                    }
                )
            )
            materials = Materials(root)
            self.assertTrue(all(v['texture_missing'] for v in materials.catalog(root)))
            with self.assertRaises(ValueError):
                materials.image('../private.png')


if __name__ == '__main__':
    unittest.main()
