"""Per-instance native PH variants. No changes to the installed game's files."""
from functools import lru_cache
import json
from pathlib import Path
import shutil
import tempfile
from ..core.path import number
from ..core import project

CATALOG = Path(__file__).resolve().parents[2] / 'data/mechanism_parameters.json'
LOADER = '3D Entities/Levelinit.nmo'


@lru_cache(maxsize=1)
def profiles():
    return json.loads(CATALOG.read_text())['profiles']


def parameters(kind, value):
    spec = profiles().get(kind, {})
    if not isinstance(value, dict) or set(value) - spec.keys():
        raise ValueError(f'{kind}: unsupported dynamics parameters')
    result = {key: item['default'] for key,item in spec.items()}
    for key,v in value.items():
        item = spec[key]
        if type(item['default']) is bool:
            if type(v) is not bool:
                raise ValueError(f'{kind}.{key} must be boolean')
        else:
            v = number(v, key)
            if not item['min'] <= v <= item['max']:
                raise ValueError(f'{kind}.{key} must be in [{item["min"]}, {item["max"]}]')
        result[key] = v
    if result.get('limits_enabled') and result['lower_angle'] >= result['upper_angle']:
        raise ValueError('A joint needs a nonzero angular range containing its initial pose')
    return result


def variant(identifier, kind, values):
    defaults = parameters(kind, {})
    if values == defaults:
        return None
    return kind + '_Edit_' + project._identity({'id':identifier, 'values':values})[:16]


def generate(layout, out, game, nmo):
    from . import motions
    items = [i for i in motions.instances(layout) if i.get('runtime_variant')]
    if not items:
        return
    if game is None or nmo is None:
        raise ValueError('Native dynamics require --game and --nmo to build the runtime resources')
    game = Path(game)
    recipes = motions.catalog()
    files, sources, records = {}, {}, []
    for item in items:
        source = game / recipes[item['kind']]['template']
        sha = project.digest(source)
        if sha != recipes[item['kind']]['sha256']:
            raise ValueError(f'{item["kind"]}: native dynamics template changed; refresh its contract')
        sources[str(source.relative_to(game))] = sha
        relative = '3D Entities/PH/' + item['runtime_variant'] + '.nmo'
        dest = out / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        # Parameter IDs are pinned by the source hash. Retain the original
        # graph, body names and IDs; rename only native loader entry points.
        operations = [{'op':'set_parameter_value', 'parameter_id':pin,
                       'value':str(v).upper() if type(v) is bool else str(v)}
                      for key,v in item['settings']['physics'].items()
                      for pin in profiles()[item['kind']][key]['pins']]
        with tempfile.TemporaryDirectory() as work:
            plan = Path(work)/'patch.json'
            project.write_json(plan, {'version':2, 'input':str(source.resolve()),
                                     'output':str(dest.resolve()), 'operations':operations})
            nmo.run('patch','apply',str(plan))
        names = {o.get('name'):o['id'] for o in nmo.data('object','list',str(dest))['objects']}
        for old,new in [(item['kind']+'_Init',item['runtime_variant']+'_Init'),
                        (item['kind']+'_MF',item['runtime_variant']+'_MF'),
                        ('All_'+item['kind'],'All_'+item['runtime_variant'])]:
            if old not in names:
                raise ValueError(f'{item["kind"]}: missing native loader entry {old}')
            nmo.run('object','rename',str(names[old]),new,str(dest),'-o',str(dest))
        files[relative] = project.digest(dest)
        records.append({'id':item['id'],'kind':item['kind'],'variant':item['runtime_variant'],
                        'template':relative,'physics':item['settings']['physics']})
    source = game/LOADER
    sources[LOADER] = project.digest(source)
    loader_contract = recipes['P_Modul_37']['stop_runtime']
    if sources[LOADER] != loader_contract['sha256']:
        raise ValueError('Native dynamics loader changed; refresh its contract')
    dest = out/LOADER
    table = {'columns':[{'name':name,'type':kind} for name,kind in
             [('Group Names','string'),('Amount','int'),('Activation','int'),('Reset','int')]],
             'rows':[[i['runtime_variant'],0,1,1] for i in items]}
    with tempfile.TemporaryDirectory() as work:
        path = Path(work)/'groups.json';project.write_json(path,table)
        nmo.run('data','fill','--append','--name','PH_Groups','--table',str(path),str(source),'-o',str(dest))
    files[LOADER] = project.digest(dest)
    project.write_json(out/'native_dynamics.json', {'schema_version':1,'instances':records,
                       'files':files,'sources':sources,
                       'deployment':'Keep the generated 3D Entities directory with the level. A bare NMO does not contain these PH variants.'})


def dependencies(folder):
    path = Path(folder)/'native_dynamics.json'
    if not path.is_file():
        return []
    return list(json.loads(path.read_text())['files'])


def copy_runtime(folder, destination):
    for name in dependencies(folder):
        target = Path(destination)/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(Path(folder)/name, target)


def joint_angles(instance, recipe):
    angles = list(recipe.get('angles', [-180,180]))
    settings = instance.get('settings',{}).get('physics',{})
    if settings.get('limits_enabled'):
        angles = [max(angles[0],settings['lower_angle']), min(angles[1],settings['upper_angle'])]
    if instance.get('stops'):
        angles = [max(angles[0],instance['stops']['angles'][0]),min(angles[1],instance['stops']['angles'][1])]
    return angles
