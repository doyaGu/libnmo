"""Document operations independent of whether the intermediate level compiles."""
from copy import deepcopy

from ballance.route.templates import expand
from ballance.route.editing import leaves, kind, validate_edit
from .model import model, locate, edited_route

PALETTE = {
    'road': ('直地面', {'road': 10}),
    'rail': ('直双轨', {'rail': 20}),
    'curve': ('贝塞尔双轨', {'rail_path': {'segments': [{'bezier': [[7,0,0],[13,0,0],[20,0,0]]}]}}),
    'beam': ('单轨', {'beam': 20}),
    'floor': ('自由地面', {'floor_path': {'segments': [{'line': [20,0,0]}], 'width': 5}}),
    'turn': ('地面转弯', {'turn': 'right'}),
    'rail_turn': ('双轨转弯', {'rail_turn': 'right'}),
    'connector': ('截面连接器', {'connector': {'length': 10}}),
    'platform': ('带洞平台', {'floor_polygon': {'outline': [[0,-8],[20,-8],[20,8],[0,8]],
        'holes': [[[7,3],[13,3],[13,6],[7,6]]], 'entry': {'edge': 3}, 'exit': {'edge': 1}}}),
}


def palette(route):
    from ballance.game.resources import catalog
    result = [{'key': k, 'label': label} for k,(label,_) in PALETTE.items()]
    result += [{'key': 'resource:'+k, 'label': k} for k,v in catalog().items()
               if 'route' in v.get('placement',{}) or 'module' in v.get('placement',{})]
    result += [{'key': 'template:'+k, 'label': '模板 / '+k} for k in route.get('definitions',{})]
    return result


def recipe(key, route):
    if key in PALETTE:
        return deepcopy(PALETTE[key][1])
    if key not in {v['key'] for v in palette(route)}:
        raise ValueError('Choose an element from the palette')
    prefix, name = key.split(':',1)
    return {'resource': name} if prefix == 'resource' else {'use': name}


def authored(route):
    def walk(items, path):
        for i,p in enumerate(items):
            yield p, f'{path}/{i}'
            for j,b in enumerate(p.get('branches',[])):
                yield from walk(b['route'], f'{path}/{i}/branches/{j}/route')
    for i,sector in enumerate(route['sectors']):
        yield from walk(sector, f'/sectors/{i}')


def view(route, previous=None):
    """An invalid intermediate document remains selectable and repairable."""
    try:
        return anchors(model(route), route), None
    except (ValueError, KeyError, TypeError, IndexError) as error:
        old = {e['id']: e for e in (previous or {}).get('entities', [])}
        entities = []
        try:
            resolved = [(p, i) for i,sec in enumerate(expand(route),1) for p in leaves([sec])]
        except (ValueError, KeyError, TypeError, IndexError):
            from ballance.route.identity import Piece, Origin
            resolved = [(Piece(p, Origin('route/id/'+p.get('id','unknown'),path,(),bool(p.get('id')))),
                         int(path.split('/')[2])+1) for p,path in authored(route)]
        cursors = {}
        for p,sector in resolved:
            source=p.origin.data();identifier=p.origin.entity_id
            existing=old.get(identifier,{})
            scope = (source['instances'][0]['pointer'] if source['instances'] else source['pointer']).rsplit('/',1)[0]
            incoming = cursors.get(scope, {'position':[0,0,0], 'heading':[1,0]})
            frame = existing.get('frame') or deepcopy(incoming)
            end = existing.get('end', incoming)
            path = existing.get('path', [])
            # A failed extrusion must not erase the editable construction curve.
            spec = next((p[k] for k in ('rail_path','beam_path','floor_path','connector')
                         if isinstance(p.get(k),dict) and p[k].get('segments')), None)
            if spec:
                try:
                    from ballance.core.path import sample_path
                    samples = sample_path(spec)
                    x,y,z = frame['position']; dx,dz = frame['heading']
                    path = [[x+a.point[0]*dx+a.point[1]*dz, y+a.point[2],
                             z+a.point[0]*dz-a.point[1]*dx] for a in samples]
                    a,b = samples[-1].direction
                    end = {'position':path[-1], 'heading':[a*dx+b*dz,a*dz-b*dx]}
                except (ValueError, KeyError, TypeError, IndexError):
                    pass
            cursors[scope] = end
            targets={}
            if source['instances']:
                targets['instance']={'piece':p,'pointer':'/overrides/'+identifier,'label':'仅此展开元素'}
                call=source['instances'][0]
                targets['call']={'piece':locate(route,call['pointer']),'pointer':call['pointer'],'label':'此模板调用'}
            else:
                targets['piece']={'piece':locate(route,source['pointer']),'pointer':source['pointer'],'label':'此路线元素'}
            entities.append({'id':identifier,'source':source,'effective':p,'piece':next(iter(targets.values()))['piece'],
                'edit_targets':targets,'editable':True,'geometry_kind':kind(p),'category':existing.get('category','geometry'),
                'sector':sector,'end':end, 'path':path,'frame':frame, 'overridden':identifier in route.get('overrides',{})})
        # Invalid parameters remain editable, with the last compiled pose as a
        # handle. They are never presented as a new compiled native pose.
        present = {e['id'] for e in entities}
        for old_entity in old.values():
            if old_entity.get('category') != 'mechanism' or old_entity.get('parent') not in present:
                continue
            item = deepcopy(old_entity)
            value = route.get('mechanisms', {}).get('instances', {}).get(item['id'], item['piece'])
            item.update(piece=value, effective=value)
            item['edit_targets']['mechanism']['piece'] = value
            entities.append(item)
        from .spatial import entity
        for key,label in [('scenery','装饰与依附配置'),('mechanisms','机关接触约束'),('death_zones','死亡区与恢复空间')]:
            value=deepcopy(route.get(key, None if key=='death_zones' else {}))
            item=entity('editor/settings/'+key,label,'constraint',0,[0,0,0],value,'/'+key,'settings')
            item['global']=True
            entities.append(item)
        return anchors({'entities':entities,'connections':{'connections':[]},'volumes':[],
                'sectors':len(route['sectors'])}, route), str(error)


def anchors(value, route):
    for i,sector in enumerate(route['sectors']):
        if sector:
            continue
        value['entities'].append({'id':f'editor/sector/{i}', 'anchor':True, 'geometry_kind':'空小节',
            'category':'geometry','sector':i+1,'end':{'position':[0,0,0],'heading':[1,0]},'path':[],
            'source':{'pointer':f'/sectors/{i}','instances':[],'stable':True},'piece':{},
            'edit_targets':{'piece':{'piece':{},'pointer':f'/sectors/{i}','label':'添加第一个元素'}},
            'editable':True,'overridden':False})
    return value


def change(route, current, action, entity_id, piece=None, scope=None, preset=None):
    if action in ('transform','snap'):
        from .transforms import transform, snap
        return (transform if action=='transform' else snap)(route,current,piece)
    entity=next((e for e in current['entities'] if e['id']==entity_id),None)
    if not entity:
        raise ValueError('Select a route element')
    entity=next((e for e in current['entities'] if e['id']==entity.get('edit_owner')),entity)
    if entity.get('anchor') and action != 'insert':
        raise ValueError('Choose an element to insert into this empty sector')
    if action == 'set':
        return edited_route(route,entity,piece,scope)[0], entity_id
    source=entity.get('source') or {}
    path=(source['instances'][0]['pointer'] if source.get('instances') else source.get('pointer',''))
    if not path.startswith('/sectors/'):
        raise ValueError('Insert, duplicate and delete operate on route elements')
    candidate=deepcopy(route)
    parent,_,index=path.rpartition('/')
    if entity.get('anchor'):
        parent,index=path,-1
    items=locate(candidate,parent);index=int(index)
    original=items[index] if items else None
    if action == 'delete':
        items.pop(index)
        prefix=source['instances'][0]['id'] if source.get('instances') else entity['id']
        for key in list(candidate.get('overrides',{})):
            if key==prefix or key.startswith(prefix+'/'):
                candidate['overrides'].pop(key)
        for key in list(candidate.get('placements',{})):
            if key==prefix or key.startswith(prefix+'/'):
                candidate['placements'].pop(key)
        for key in list(candidate.get('mechanisms', {}).get('instances', {})):
            if key.startswith(prefix+'/'):
                candidate['mechanisms']['instances'].pop(key)
        return candidate, None
    if action not in ('insert','duplicate'):
        raise ValueError('Unknown document operation')
    if action == 'duplicate' and ('branches' in original or 'name' in original):
        raise ValueError('Named junctions need explicit connections; insert a new element instead')
    value=deepcopy(original) if action=='duplicate' else recipe(preset,route)
    if action == 'insert' and piece is not None:
        validate_edit(value, piece)
        value = deepcopy(piece)
    used={p.get('id') for p,_ in authored(route)}
    serial=1
    while f'edit-{serial:03d}' in used:serial+=1
    value['id']=f'edit-{serial:03d}'
    items.insert(index+1,value)
    # Branch IDs include their parent scope; insertion never renames siblings.
    prefix=source['instances'][0]['id'] if source.get('instances') else entity['id']
    identifier=('route' if entity.get('anchor') else prefix.rsplit('/id/',1)[0])+'/id/'+value['id']
    if action=='duplicate' and 'use' in value:
        for key,patch in list(candidate.get('overrides',{}).items()):
            if key.startswith(prefix+'/'):
                candidate['overrides'][identifier+key[len(prefix):]]=deepcopy(patch)
    if action=='duplicate':
        for key,value in list(candidate.get('mechanisms', {}).get('instances', {}).items()):
            if key.startswith(prefix+'/'):
                candidate['mechanisms']['instances'][identifier+key[len(prefix):]] = deepcopy(value)
    selected=next((p.origin.entity_id for p in leaves(expand(candidate))
                   if p.origin.entity_id==identifier or p.origin.entity_id.startswith(identifier+'/')),identifier)
    return candidate, selected
