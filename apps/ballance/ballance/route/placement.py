"""Explicit entry poses preserve independently placed route pieces.

A moved entry breaks adjacency, not the editable document. Geometry can still
be exported; disconnected ports remain explicit in the connection report.
Native grid pieces keep their cardinal orientation requirement.
"""
from dataclasses import replace
import math
from . import ports
from ..core.path import vector
from ..kit.rails import Ring, Beam, ABOVE_V


def validate(values):
    if not isinstance(values, dict):
        raise ValueError('placements must map entity IDs to entry poses')
    result = {}
    for key, value in values.items():
        if not isinstance(key,str) or not isinstance(value,dict) or set(value) != {'position','heading'}:
            raise ValueError('a placement needs position and heading')
        position = vector(value['position'], 'placement position')
        heading = value['heading']
        if (not isinstance(heading,(list,tuple)) or len(heading)!=2
                or any(type(v) not in (int,float) or not math.isfinite(v) for v in heading)
                or abs(math.hypot(*heading)-1)>1e-6):
            raise ValueError('placement heading must be a finite unit direction')
        result[key] = {'position':position,'heading':tuple(heading)}
    return result


def begin(layout, sector, piece, track):
    identifier = piece.origin.entity_id
    original = ports.incoming(layout,track)
    value = layout.placements.get(identifier)
    changed = value and (math.dist(original.position,value['position'])>1e-6
                         or math.dist(original.heading,value['heading'])>1e-6)
    if changed:
        free = any(k in piece for k in ('rail_path','floor_path','floor_polygon','connector'))
        if not free and ports.cardinal(value['heading']) is None:
            raise ValueError('native grid pieces rotate in 90 degree steps; use a free path for arbitrary headings')
        x,y,z=value['position']; dx,dz=value['heading']
        if track.run is not None:
            sector.rails.runs.append(track.run)
            last=track.run[-1]
            ring=Ring(x,y+ABOVE_V,z,dx,dz,last.bank,last.gauge,last.radius)
            track.run=Beam([ring]) if isinstance(track.run,Beam) else [ring]
        track.p,track.h,track.d=(x,z),y,(dx,dz)
        if track.free_port or free:
            track.free_port=replace(track.free_port or original,position=(x,y,z),heading=(dx,dz))
        layout.floors=sector.grid(track.p)
    pose={'position':[track.p[0],track.h,track.p[1]],'heading':list(track.d)}
    layout.entry_poses[identifier]=pose
    return original, bool(changed), len(layout.connections)


def finish(layout,piece,track,context):
    original, changed, offset=context
    identifier=piece.origin.entity_id
    recorded=layout.connections[offset:]
    incoming=next((c.after for c in recorded if c.after.owner==identifier),None)
    if incoming is None:
        # Measured module/beam interiors are not ordinary cross-section seams.
        incoming=ports.Port(identifier,'entry',tuple(layout.entry_poses[identifier]['position']),
                            tuple(layout.entry_poses[identifier]['heading']),original.section,piece.origin)
    outgoing=replace(ports.incoming(layout,track),owner=identifier,name='exit',source=piece.origin)
    layout.edit_ports[identifier]={'entry':incoming.data(),'exit':outgoing.data(),
        'snappable':any(k in piece for k in ('road','rail','rail_path','floor_path','floor_polygon','connector','turn','rail_turn'))}
    if not changed:
        return
    # Replace the synthetic incoming joint created while laying detached geometry
    # with a check against the real previous exit. Never claim a fake connection.
    layout.connections[offset:]=[c for c in recorded if c.after.owner!=identifier]
    try:
        connection=ports.connect(original,incoming)
        layout.connections.append(connection)
    except ports.ConnectionError as error:
        layout.open_connections.append({'from':original.data(),'to':incoming.data(),
                                        'reason':error.rule,'distance':math.dist(original.position,incoming.position)})
