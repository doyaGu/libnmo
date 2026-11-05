"""Static passage clearance, landing support, and native motion reservations."""

from ..core.space import Space, sweep_hit
from ..core.surface import Surfaces
from ..game.measures import BALL_RADIUS
from ..route import motions
from ..route.passages import landing_points, lanes, samples, volumes
from ..route.spaces import route_space

SOLID_GROUPS = {'Phys_Floors', 'Phys_FloorStopper', 'Phys_FloorRails'}


def check(scene, layout):
    solids = Surfaces(
        (obj.name, triangle.points)
        for obj in scene
        if SOLID_GROUPS.intersection(obj.groups)
        for triangle in obj.triangles
    )
    floors = Surfaces(
        (obj.name, triangle.points)
        for obj in scene
        if 'Phys_Floors' in obj.groups
        for triangle in obj.triangles
    )
    findings, corridors = {}, []

    def record(key, value):
        findings.setdefault(key, value)

    # Rail support tubes are intentionally tangent to the ball. Test them too:
    # no blanket owner or neighbouring-piece exemption hides an overhead rail.
    rails = [v for v in route_space(layout, mechanisms=False).volumes if v.rule == 'ball clearance']
    support_fits = {}
    for volume in rails:
        hits = solids.clearance(*volume.segment, volume.radius)
        if hits and solids.supporting_contacts(*volume.segment, volume.radius):
            # A nominal rail centre is lower than the resting sphere at a
            # floor joint or a faceted bend. Fit upward by at most one ball
            # radius, checking all obstacles again (including any ceiling).
            for step in range(1, 11):
                lift = BALL_RADIUS * step / 10
                raised = tuple((p[0], p[1] + lift, p[2]) for p in volume.segment)
                if not solids.clearance(*raised, volume.radius):
                    support_fits[volume.owner] = max(support_fits.get(volume.owner, 0), lift)
                    hits = {}
                    break
        for obstacle, penetration in hits.items():
            record(
                (volume.owner, 'ball clearance', obstacle),
                {
                    'owner': volume.owner,
                    'rule': 'ball clearance',
                    'obstacle': obstacle,
                    'penetration': penetration,
                    'segment': volume.segment,
                    'source': volume.source,
                    'severity': 'review',
                },
            )

    for passage in layout.passages:
        missing, tested = [], 0
        segments = list(lanes(passage))
        for a, b in segments:
            for obstacle, penetration in solids.clearance(a, b, BALL_RADIUS).items():
                record(
                    (passage['id'], 'clearance', obstacle),
                    {
                        'passage': passage['id'],
                        'owner': passage['owner'],
                        'rule': 'ball clearance',
                        'obstacle': obstacle,
                        'penetration': penetration,
                        'segment': [a, b],
                        'severity': 'error',
                    },
                )
        support_points = (
            (point for a, b in segments for point in samples(a, b))
            if passage['kind'] == 'floor'
            else landing_points(passage)
        )
        for point in support_points:
            tested += 1
            foot = (point[0], point[1] - BALL_RADIUS, point[2])
            if floors.support(foot) is None:
                if len(missing) < 12:
                    missing.append(foot)
        if missing:
            record(
                (passage['id'], 'support'),
                {
                    'passage': passage['id'],
                    'owner': passage['owner'],
                    'rule': 'landing support' if passage['kind'] == 'landing' else 'floor support',
                    'positions': missing,
                    'severity': 'error',
                },
            )
        if passage['kind'] == 'landing':
            for point in landing_points(passage):
                for obstacle, penetration in solids.clearance(point, point, BALL_RADIUS).items():
                    record(
                        (passage['id'], 'landing clearance', obstacle),
                        {
                            'passage': passage['id'],
                            'owner': passage['owner'],
                            'rule': 'landing clearance',
                            'obstacle': obstacle,
                            'position': point,
                            'penetration': penetration,
                            'severity': 'error',
                        },
                    )
        corridors.append(
            {
                **passage,
                'support_samples': tested,
                'status': 'failed'
                if any(f.get('passage') == passage['id'] for f in findings.values())
                else 'passed',
            }
        )

    occupied = Space()
    items, recipes = motions.instances(layout), motions.catalog()
    for item in items:
        for volume in motions.volumes(item, recipes.get(item['kind'])):
            occupied.add(volume)
    rules = motions.interactions(layout, items)
    contacts, expected, seen = [], [], set()
    for corridor in volumes(layout.passages):
        for hit in occupied.query(corridor):
            phase = hit.get('phase', hit['state'])
            key = (corridor.source['passage'], hit['owner'], phase, hit['kind'])
            if key in seen:
                continue
            # Space's box broad phase does not use a capsule probe against boxes.
            # Keep only native envelopes touched by the actual centre segment.
            candidates = [
                v
                for v in occupied.volumes
                if v.owner == hit['owner'] and v.rule == hit['kind'] and v.state == hit['state']
            ]
            if not any(sweep_hit(*corridor.segment, v.box, corridor.radius) for v in candidates):
                continue
            seen.add(key)
            reason = motions.intention(
                rules,
                hit['owner'],
                corridor.owner,
                corridor.rule,
                phase,
                corridor.source['passage'],
            )
            contact = {
                'passage': corridor.source['passage'],
                'owner': corridor.owner,
                'rule': corridor.rule,
                'contact': hit,
                'conservative': True,
            }
            if reason:
                expected.append(contact | {'design_reason': reason})
            else:
                contacts.append(contact)
    findings = list(findings.values())
    unbounded = [c for item in items for c in motions.unbounded(item, recipes.get(item['kind']))]
    return {
        'status': 'failed'
        if any(f['severity'] == 'error' for f in findings)
        else 'review'
        if findings or contacts or unbounded
        else 'passed',
        'findings': findings,
        'passages': corridors,
        'motion_contacts': contacts,
        'expected_interactions': expected,
        'unbounded_motion': unbounded,
        'rail_support_fits': support_fits,
        'rail_segments': len(rails),
        'floor_passages': sum(p['kind'] == 'floor' for p in corridors),
        'landings': sum(p['kind'] == 'landing' for p in corridors),
        'scope': 'Continuous sphere sweeps against physical world triangles; '
        '0.05-unit penetration tolerance. Authored floor lanes and landing discs '
        'sampled at most 0.5 units apart for support at the declared layer '
        '(0.25 height tolerance). Native motion envelopes cover all declared '
        'phases, including the owning mechanism; intersections need review. '
        'Rail support contacts allow an upward fit of at most one ball radius; '
        'reported fits are geometric feasibility, not dynamic proof. '
        'Unauthored floor routes, ballistic dynamics, between-lane gaps and '
        'camera clearance are not proven.',
    }
