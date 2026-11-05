"""Serve only manifest-referenced game textures, retaining original alpha."""

from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
from PIL import Image


class Materials:
    def __init__(self, game):
        self.folder = (game / 'Textures').resolve() if game else None
        self.paths = {}
        self.pngs = {}

    def catalog(self, folder, extra=()):
        values = [*json.loads((folder / 'manifest.json').read_text())['materials'], *extra]
        files = (
            {p.name.casefold(): p for p in self.folder.iterdir() if p.is_file()}
            if self.folder and self.folder.is_dir()
            else {}
        )
        result = []
        for material in values:
            value = dict(material)
            name = material.get('texture')
            value['texture_url'] = None
            path = (
                files.get(Path(name).name.casefold()) if name and Path(name).name == name else None
            )
            if path and path.resolve().is_relative_to(self.folder):
                digest = sha256(path.read_bytes()).hexdigest()
                self.paths[digest] = path
                value['texture_url'] = '/texture/' + digest + '.png'
            value['texture_missing'] = bool(name and not value['texture_url'])
            result.append(value)
        return result

    def image(self, digest):
        if digest not in self.paths:
            raise ValueError('Unknown material texture')
        if digest not in self.pngs:
            payload = self.paths[digest].read_bytes()
            if sha256(payload).hexdigest() != digest:
                raise ValueError('Texture changed; refresh scene')
            output = BytesIO()
            with Image.open(BytesIO(payload)) as image:
                image.convert('RGBA').save(output, format='PNG')
            self.pngs[digest] = output.getvalue()
        return self.pngs[digest]
