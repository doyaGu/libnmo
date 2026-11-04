"""Resolve native hinge stops from the actual generated module surfaces."""

from ..core.motion import place, hinge_stops
from ..core.nmo import place as matrix_place
from ..core.obj import read_obj
from ..core.project import digest
from . import motions
from .modules import MODULE_FLOORS
from .spaces import part_id


def prepare(layout, assets):
    # Removing or moving a floor must never retain an earlier limit.
    layout.motion_limits={}
    if assets is None or not assets.game:
        return
    recipes=motions.catalog()
    parts={part_id(p):p for p in layout.parts}
    for item in motions.instances(layout):
        recipe=recipes.get(item['kind'],{})
        body=recipe.get('bodies',{}).get(recipe.get('body'),{})
        if recipe.get('type')!='hinge' or not body.get('collision_hulls'):
            continue
        hulls=[[matrix_place(body['matrix'],p) for p in hull['points']] for hull in body['collision_hulls']]
        faces=[];dependencies={}
        for kind,_,position,yaw in parts[item['parent']].module_floors:
            for floor in MODULE_FLOORS[kind]:
                # Stoppers exclude ball collisions, not mechanism collisions.
                if not set(floor.groups)&{'Phys_Floors','Phys_FloorStopper'}:
                    continue
                path=assets.out/assets.mesh(floor.name)
                dependencies[path.name]=digest(path)
                for triangle in read_obj(path):
                    points=[]
                    for p in triangle.points:
                        world=place(p,position,yaw)
                        relative=[a-b for a,b in zip(world,item['position'])]
                        points.append(place(relative,(0,0,0),-item['yaw']))
                    faces.append({'surface':floor.name,'points':points})
        limits=hinge_stops(hulls,recipe['pivot'],recipe['axis'],faces)
        if limits:
            layout.motion_limits[item['id']]={**limits,'dependencies':dependencies,
                'basis':'Native convex collision chords cross actual module floor/stopper faces; continuous hinge motion cannot cross these barriers.'}
