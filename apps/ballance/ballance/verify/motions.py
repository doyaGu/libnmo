"""Check native motion reach against other route owners and mechanisms."""

from ..core import project
from ..core.space import Space, Volume
from ..route import motions
from ..route.spaces import route_space, part_id
from ..route.clashes import part_boxes
from ..route.motion_stops import prepare


def check(layout, assets, game=None):
    prepare(layout,assets)
    records=motions.describe(layout)
    recipes=motions.catalog()
    items=records['instances']
    space=route_space(layout, mechanisms=False)
    # Existing floor adapters are conservative, but preserve Part ownership.
    # Internal floors, rods and the ball path are intentional contacts only
    # for the owning route Part; unrelated adjacent Parts remain checked.
    for part in layout.parts:
        if not (part.floors or part.extras or part.module_floors):
            continue
        for box in part_boxes(part,assets)[0]:
            space.add(Volume(part_id(part),'route solid',box,source={
                'entity_id':part_id(part),'source':part.origin.data() if part.origin else None}))
    findings=[]
    for item in items:
        for volume in motions.volumes(item,recipes.get(item['kind'])):
            for hit in space.query(volume,ignore=(item['parent'],)):
                findings.append({'mechanism':item['id'],'parent':item['parent'],'kind':item['kind'],
                                 'phase':volume.source['phase'],'body':volume.source['body'],
                                 'rule':volume.rule,'source':item['source'],'contact':hit})
    # A rotating arm and its drawbridge form an authored coupled mechanism.
    occupied=Space()
    for item in items:
        for volume in motions.volumes(item,recipes.get(item['kind'])):
            occupied.add(volume)
    seen=set()
    for item in items:
        for volume in motions.volumes(item,recipes.get(item['kind'])):
            for hit in occupied.query(volume,ignore=(item['id'],)):
                if hit.get('parent')==item['parent'] and {item['kind'],hit['piece']}=={'P_Modul_17','P_Modul_25'}:
                    continue
                pair=tuple(sorted((item['id'],hit['owner'])))
                if pair in seen:
                    continue
                seen.add(pair)
                findings.append({'mechanism':item['id'],'parent':item['parent'],'kind':item['kind'],
                                 'phase':volume.source['phase'],'body':volume.source['body'],
                                 'rule':'mechanism interaction','source':item['source'],'contact':hit})
    unique={}
    for f in findings:
        key=(f['mechanism'],f['phase'],f['rule'],f['contact']['owner'],f['contact']['kind'])
        unique.setdefault(key,f)
    mismatch=[]
    if game is not None:
        for kind,template in records['templates'].items():
            path=game/template['template']
            if not path.is_file() or project.digest(path)!=template['sha256']:
                mismatch.append(kind)
    findings,expected=[],[]
    for f in unique.values():
        reason=motions.intention(records['interactions'],f['mechanism'],f['contact']['owner'],f['contact']['kind'],f['phase'])
        (expected if reason else findings).append(f | ({'design_reason':reason} if reason else {}))
    unsupported=[i['id'] for i in items if i['coverage']=='unsupported']
    unbounded=[c for i in items for c in motions.unbounded(i,recipes.get(i['kind']))]
    return {**records,'status':'failed' if mismatch else 'review' if findings or unsupported or unbounded else 'passed',
            'unbounded_motion':unbounded,'findings':findings,'expected_interactions':expected,'unsupported':unsupported,'template_mismatches':mismatch,
            'template_check':'checked' if game is not None else 'not requested',
            'modeled':sum(i['coverage']=='modeled' for i in items)}
