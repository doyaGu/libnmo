"""Editor transactions operate on a draft; saving the authored route is explicit."""
import json
from copy import deepcopy
import subprocess
import tempfile
import re
from pathlib import Path

from ballance.core import project, evidence
from ballance.route.build import build
from .model import model, edited_route, impact
from .spatial import compiled
from . import commands
from ballance.verify import report
from ballance.view.scene import load_project


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))



class Session:
    def __init__(self, source, workspace, elements, nmo, game=None):
        self.source, self.root = source.resolve(), workspace.resolve()
        if self.root == self.source.parent or self.root in self.source.parents:
            raise ValueError('Use a dedicated editor workspace, separate from the route directory')
        self.root.mkdir(parents=True, exist_ok=True)
        self.current = self.root / 'current'
        self.elements, self.nmo = elements, nmo
        self.game = game.resolve() if game else None
        self.native = None
        from .local_play import LocalPlay
        self.local_play = LocalPlay(self)
        from .materials import Materials
        self.materials = Materials(self.game)
        from .native import NativePreview
        self.native_preview = NativePreview(self.game, self.nmo, self.root / "native-preview-cache")
        self.source_hash = project.digest(self.source)
        metadata = self.root / 'session.json'
        if metadata.exists():
            old = read(metadata)
            if old['source'] != str(self.source) or old['source_hash'] != self.source_hash:
                raise ValueError('Source changed since this draft was created; use a new workspace')
        self.route = read(self.root / 'draft.json') if metadata.exists() else read(self.source)
        self.undo = old.get('undo', []) if metadata.exists() else []
        self.redo = old.get('redo', []) if metadata.exists() else []
        self.edit_error = old.get('edit_error') if metadata.exists() else None
        self.before = self.root / old['before'] if metadata.exists() and old.get('before') else None
        if self.before and not self.before.is_relative_to(self.current / '.history'):
            raise ValueError('Invalid previous-generation path')
        self.before_model = compiled(model(read(self.before / 'route.json')), self.before) if self.before else None
        self.impact = old.get('impact') if metadata.exists() else None
        self._persist()

    def _persist(self):
        project.write_json(self.root / 'draft.json', self.route)
        project.write_json(self.root / 'session.json', {'source': str(self.source), 'source_hash': self.source_hash,
            'before': str(self.before.relative_to(self.root)) if self.before else None, 'impact': self.impact, 'undo': self.undo, 'redo': self.redo, 'edit_error': self.edit_error})

    def revision(self):
        return project._identity(self.route)

    def _guard(self, revision):
        if revision != self.revision():
            raise ValueError('Draft changed; refresh before applying another edit')
        if self.native and self.native.poll() is None:
            raise ValueError('Close the native preview before editing or running verification')

    def document_view(self):
        previous = model(read(self.current / 'route.json')) if (self.current / 'route.json').exists() else None
        value, error = commands.view(self.route, previous)
        if not error and previous is not None and read(self.current / 'route.json') == self.route:
            value = compiled(value, self.current)
        if previous is not None and read(self.current / 'route.json') != self.route:
            # Keep generated decoration handles available when a placement fails
            # clearance. Geometry remains explicitly labelled as the last build.
            old_view = compiled(deepcopy(previous), self.current)
            known = {e['id'] for e in value['entities']}
            for e in old_view['entities']:
                if e['id'] in known or not e['id'].startswith('editor/rule/'):
                    continue
                identifier = e['id'].removeprefix('editor/rule/')
                rule = self.route.get('scenery',{}).get('rules',{}).get(identifier,e['piece'])
                e['piece']=e['effective']=deepcopy(rule)
                e['edit_targets']['rule']['piece']=deepcopy(rule)
                if 'position' in rule:e['end']['position']=list(rule['position'])
                if 'yaw' in rule:e['details']['turn']=rule['yaw']
                value['entities'].append(e)
        return value, error or self.edit_error

    def edit(self, action, entity_id, revision, piece=None, scope=None, preset=None):
        self._guard(revision)
        self.local_play.close()
        current, _ = self.document_view()
        if action in ('undo', 'redo'):
            stack = self.undo if action == 'undo' else self.redo
            if not stack:
                raise ValueError('No document operation to ' + action)
            candidate = deepcopy(stack[-1])
            (self.redo if action == 'undo' else self.undo).append(deepcopy(self.route))
            stack.pop()
            selection = entity_id
        else:
            candidate, selection = commands.change(self.route, current, action, entity_id, piece, scope, preset)
            if candidate == self.route:
                return self.state()
            self.undo = (self.undo + [deepcopy(self.route)])[-100:]
            self.redo = []
        self.route = candidate
        self.edit_error = None
        self._persist()  # Invalid intermediate geometry remains an editable draft.
        try:
            history = set((self.current / '.history').iterdir()) if (self.current / '.history').exists() else set()
            if build(candidate, self.current, self.elements, 'Rail editor', incremental=True, game=self.game, nmo=self.nmo):
                raise ValueError('Missing native resources')
            new = set((self.current / '.history').iterdir()) - history
            self.before = next(iter(new)) if new else None
            self.before_model = compiled(model(read(self.before / 'route.json')), self.before) if self.before else None
            self.impact = {'changed_entities': [entity_id], 'action': action}
            self.validate()
        except (ValueError, KeyError, TypeError, IndexError, OSError, RuntimeError) as error:
            self.edit_error = str(error)
        self._persist()
        return self.state() | {'selection': selection}

    def prepare(self):
        if (not (self.current / 'route.json').is_file()
                or read(self.current / 'route.json') != self.route
                or project.inspect(self.current)['status'] != 'passed'):
            try:
                if build(self.route, self.current, self.elements, 'Rail editor', incremental=True, game=self.game, nmo=self.nmo):
                    raise ValueError('Build failed; check extracted resources')
                self.edit_error = None
                self.validate()
            except (ValueError, KeyError, TypeError, IndexError) as error:
                self.edit_error = str(error)
                self._persist()

    def state(self):
        checks = {}
        for name in ('validation.json', 'partial-validation.json', 'full-validation.json'):
            path = self.current / name
            if path.exists():
                result = read(path)
                checks[name] = {'status': result['status'], 'freshness': evidence.inspect(path),
                                'checks': result['checks'], 'scope': result.get('evidence', {}).get('parameters')}
        document, error = self.document_view()
        return {'document_error': error, 'undo_count': len(self.undo), 'redo_count': len(self.redo),
                'palette': commands.palette(self.route), 'revision': self.revision(), 'source': str(self.source), 'workspace': str(self.root),
                'saved': read(self.source) == self.route,
                'model': document, 'checks': checks, 'impact': self.impact,
                'build': project.inspect(self.current),
                'rebuild': read(self.current / 'rebuild.json'),
                'before_available': self.before is not None,
                'native_running': bool(self.native and self.native.poll() is None),
                'local_starts': self.local_play.starts()}

    def preview(self, entity_id, piece, revision, scope=None, reset=False):
        self._guard(revision)
        previous = compiled(model(self.route), self.current)
        entity = next((e for e in previous['entities'] if e['id'] == entity_id), None)
        if not entity or not entity['editable']:
            raise ValueError('Select an editable geometry element with a stable ID')
        owner = next((e for e in previous['entities'] if e['id'] == entity.get('edit_owner')), entity)
        candidate, scope, pointer = edited_route(self.route, owner, piece, scope, reset)
        after = model(candidate)
        preview_checks = None
        if (entity.get('category') != 'geometry' or scope in ('settings', 'placement', 'rule')
                or owner.get('effective', {}).get('elements') != next(
                    (e.get('effective', {}).get('elements') for e in after['entities'] if e['id'] == owner['id']), None)):
            # An invalid placement must fail before publishing a generation or
            # superseding the last valid gameplay evidence.
            with tempfile.TemporaryDirectory(prefix='.preview-', dir=self.root) as tmp:
                folder = Path(tmp)
                if build(candidate, folder, self.elements, 'Rail editor', game=self.game, nmo=self.nmo):
                    raise ValueError('Spatial preview could not build native resources')
                after = compiled(after, folder)
                result = report.verify(folder, self.nmo, game=self.game)
                preview_checks = {k: v for k, v in result['checks'].items() if k in
                                  ('geometry', 'connections', 'scenery', 'mechanisms', 'resources')}
        else:
            # Never compare compiled accessories to an absent candidate list.
            previous = model(self.route)
        return candidate, {'model': after, 'impact': impact(previous, after, entity_id, scope, pointer),
                           'revision': revision, 'spatial_checks': preview_checks}

    def apply(self, entity_id, piece, revision, scope=None, reset=False):
        candidate, preview = self.preview(entity_id, piece, revision, scope, reset)
        failed = [key for key, check in (preview['spatial_checks'] or {}).items() if check['status'] == 'failed']
        if failed:
            raise ValueError('Spatial preview failed; draft retained: ' + ', '.join(failed))
        self.local_play.close()
        old_revision = self.revision()
        before_model = compiled(model(self.route), self.current)
        history = set((self.current / '.history').iterdir())
        if build(candidate, self.current, self.elements, 'Rail editor', incremental=True, game=self.game, nmo=self.nmo):
            raise ValueError('Build failed; the previous generation was retained')
        new_history = set((self.current / '.history').iterdir()) - history
        self.before = next(iter(new_history))
        self.before_model = before_model
        self.undo = (self.undo + [deepcopy(self.route)])[-100:]
        self.redo = []
        self.edit_error = None
        self.route = candidate
        self.impact = preview['impact'] | {'before_revision': old_revision}
        self._persist()
        self.validate()
        return self.state()

    def validate(self, sector=1, frames=None):
        self._guard(self.revision())
        if read(self.current / 'route.json') != self.route:
            raise ValueError('Repair the draft before running gameplay verification')
        if not isinstance(sector, int) or not 1 <= sector <= len(self.route['sectors']):
            raise ValueError('Invalid sector')
        if frames is not None and (not isinstance(frames, int) or not 1 <= frames <= 100000):
            raise ValueError('Frames must be between 1 and 100000')
        if frames is not None and self.document_view()[0]['connections'].get('open_connections'):
            raise ValueError('先对齐未连接的端口，再运行地图。')
        if frames is not None and not self.game:
            raise ValueError('Gameplay requires --game')
        path = self.current / ('validation.json' if frames is None else
                               'full-validation.json' if sector == 1 else 'partial-validation.json')
        try:
            result = report.verify(self.current, self.nmo, frames=frames, game=self.game, sector=sector)
            report.write(result, path)
        except Exception as error:
            report.write({'status': 'failed', 'checks': {'input': {'status': 'failed', 'error': str(error)}}}, path)
            raise
        return result

    def save(self, revision):
        self._guard(revision)
        if self.document_view()[1]:
            raise ValueError('Draft saved in workspace; repair it before exporting to the source route')
        if self.document_view()[0]['connections'].get('open_connections'):
            raise ValueError('草稿已保留；先吸附或修复断开的连接，再保存到源地图。')
        if project.digest(self.source) != self.source_hash:
            raise ValueError('Source was edited externally; refusing to overwrite it')
        project.write_json(self.source, self.route)
        self.source_hash = project.digest(self.source)
        self._persist()
        return self.state()

    def scene(self, before=False):
        folder = self.before if before else self.current
        if not folder:
            raise ValueError('No previous generation')
        objects = []
        native, native_materials, replaced, instances = self.native_preview.scene(folder)
        current_view = self.before_model if before else self.document_view()[0]
        scenery_sectors = {name:e['sector'] for e in current_view['entities']
                           for name in e.get('details', {}).get('objects', [])}
        spatial_path = folder / 'spatial_constraints.json'
        assemblies = {name for binding in read(spatial_path)['attachments'] for name in binding['objects']} if spatial_path.exists() else set()
        for obj in load_project(folder):
            if obj.name == 'SkyLayer' or obj.name in replaced:
                continue
            match = re.match(r'A(\d+)_', obj.name)
            sector = next((int(g.split('_')[1]) for g in obj.groups if g.startswith('Sector_')),
                          int(match[1]) if match else scenery_sectors.get(obj.name, 0))
            color = [0.62, 0.76, 0.84] if 'Phys_FloorRails' in obj.groups else [0.48, 0.51, 0.55]
            if 'Phys_Floors' not in obj.groups and 'Phys_FloorRails' not in obj.groups:
                color = [0.65, 0.48, 0.29]
            objects.append({'batches':self.batches(obj.triangles), 'name': obj.name, 'sector': sector, 'color': color,
                            'play_surface':'rail' if 'Phys_FloorRails' in obj.groups else 'floor' if 'Phys_Floors' in obj.groups else None,
                            'decoration': obj.name in assemblies or ('Phys_Floors' not in obj.groups and 'Phys_FloorRails' not in obj.groups)})
        from ballance.core.obj import Triangle
        for obj in native:
            triangles=[Triangle(**t) for t in obj.pop('triangles')]
            objects.append(dict(obj,batches=self.batches(triangles),color=[.6,.6,.6]))
        return {'objects': objects, 'materials':self.materials.catalog(folder,native_materials), 'model': current_view,
                'native_instances':instances,
                'view': 'Real native template meshes; recorded physical poses are separate from authored placement.'}

    @staticmethod
    def batches(triangles):
        batches = {}
        for triangle in triangles:
            batch = batches.setdefault(triangle.material, {'material':triangle.material, 'vertices':[], 'uvs':[], 'normals':[]})
            batch['vertices'].extend(round(c,5) for p in triangle.points for c in p)
            batch['uvs'].extend(c for uv in (triangle.uvs or [(0,0)]*3) for c in uv)
            if triangle.normals:
                normals = triangle.normals
            else:
                a,b,c = triangle.points
                u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
                n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
                normals=[n]*3
            batch['normals'].extend(round(c,5) for n in normals for c in n)
        return list(batches.values())

    def launch(self, sector, handoff=None):
        self._guard(self.revision())
        if not self.game or not isinstance(sector, int) or not 1 <= sector <= len(self.route['sectors']):
            raise ValueError('Select a valid sector and supply --game')
        if read(self.current / 'route.json') != self.route or project.inspect(self.current)['status'] != 'passed':
            raise ValueError('Rebuild the draft before opening the game window')
        if self.document_view()[0]['connections'].get('open_connections'):
            raise ValueError('先修复断开的连接，再试玩。')
        script = Path(project.__file__).resolve().parents[2] / 'scripts/ballance_play_level.py'
        command = ['python3', str(script), str(self.current), '--sector', str(sector),
                   '--game', str(self.game), '--nmo', self.nmo.executable]
        if handoff is not None:
            if not isinstance(handoff, int) or not 1 <= handoff <= 100000:
                raise ValueError('Invalid handoff frame')
            command += ['--handoff-frame', str(handoff)]
        with (self.root / 'native-preview.log').open('w') as log:
            self.native = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        return {'status': 'launched', 'sector': sector, 'pid': self.native.pid}
