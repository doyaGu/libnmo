"""Repeatable local motion probes in original levels, using native IVP graphs.

The level copy is byte-identical. A separate harness places the probe ball
only after the native sector has activated. This is not a full-level play test.
"""

import math
import csv
import shutil
import subprocess
from pathlib import Path
from types import SimpleNamespace

from ..core.nmo import field_values, matrix_of
from ..core.motion import place
from ..core.project import digest, write_json
from ..game.resources import resource
from ..route.layout import Part
from ..route import motions
from . import harness, motion_trace

# Local ball start and target: excite the native body by gravity/contact.
PROBES = {
    'Pusher': ((-12, -1, 0.65), (0, -1, 0.65), 'Stone'),
    'Lift': ((3, -6, 0), (0, -6, 0), 'Wood'),
    'Swing': ((3, 2, 0), (-3, 2, 0), 'Wood'),
    'Flaps': ((-0.5, 3, -3), (-4, 3, -3), 'Stone'),
    'Plates': ((1, 3, 0), (-4, 3, 0), 'Stone'),
    'Seesaw': ((5, 7, 0), (-5, 7, 0), 'Stone'),
    'StoneBox': ((-7.7, -4, 15), (-7.7, -4, -15), 'Stone'),
}


def marker_layout(source, nmo, kind):
    groups = nmo.groups(source)
    markers = {o['id']: o for o in nmo.objects(source, 'CK3dEntity')}
    parts = []
    records = []
    for member in groups.get(kind, []):
        obj = markers[member['id']]
        matrix = matrix_of(field_values(obj)['world_matrix'])
        if len(matrix) != 16 or not all(math.isfinite(v) for v in matrix):
            raise ValueError(f'{obj["name"]}: invalid native marker matrix')
        yaw = math.degrees(math.atan2(matrix[8], matrix[0]))
        expected = [
            *place((1, 0, 0), (0, 0, 0), yaw),
            0,
            0,
            1,
            0,
            0,
            *place((0, 0, 1), (0, 0, 0), yaw),
            0,
            *matrix[12:15],
            1,
        ]
        if len(matrix) != 16 or any(abs(a - b) > 1e-4 for a, b in zip(matrix, expected)):
            raise ValueError(
                f'{obj["name"]}: tilted/scaled native marker needs a full-frame adapter'
            )
        sectors = [
            int(g.split('_')[1])
            for g, ms in groups.items()
            if g.startswith('Sector_') and any(m['id'] == obj['id'] for m in ms)
        ]
        if len(sectors) != 1:
            raise ValueError('native mechanism needs exactly one sector')
        sector = sectors[0]
        parts.append(
            Part(len(parts), sector, obj['name'], modules=[(kind, sector, matrix[12:15], yaw)])
        )
        records.append({'name': obj['name'], 'matrix': matrix, 'sector': sector})
    if not parts:
        raise ValueError(f'no native markers for {kind}')
    return SimpleNamespace(
        parts=parts,
        sectors=[None] * max(r['sector'] for r in records),
        balloon=None,
        mechanism_settings={},
    ), records


def probe(name, game, out, nmo, frames=1600, *, recovery=False):
    if recovery and name != 'Plates':
        raise ValueError('The recovery scenario currently covers Plates only')
    spec = resource(name)
    extract = spec['parts'][0]['extract']
    source = game / extract['file']
    kind = spec['placement']['module']
    layout, markers = marker_layout(source, nmo, kind)
    index = next(i for i, m in enumerate(markers) if m['name'] == extract['object'])
    marker = markers[index]
    instance = motions.instances(layout)[index]
    start, target, ball = PROBES[name]
    start = place(start, instance['position'], instance['yaw'])
    target = place(target, instance['position'], instance['yaw'])
    out.mkdir(parents=True, exist_ok=True)
    level = out / 'Level_01.NMO'
    sector = marker['sector']
    before = digest(source)
    shutil.copyfile(source, level)
    base = out / 'probe_base.cmo'
    ball_file = game / '3D Entities/Balls.nmo'
    table = nmo.data('data', 'dump', '--name', 'Physicalize_GameBall', str(ball_file))
    row = next(r for r in table['rows'] if r[0] == f'Ball_{ball}')
    physics = {
        'Friction': row[1],
        'Elasticity': row[2],
        'Mass': row[3],
        'Collision Group': row[4],
        'Linear Speed Dampening': row[5],
        'Rot Speed Dampening': row[6],
        'Automatic Calculate Mass Center': False,
    }
    harness.write(base, nmo, sector, ball, probe_position=start, probe_physics=physics)
    first = 370 + math.ceil(harness.setup_ms(sector) / (1000 / 60))
    steer = f'{start[0]},{start[2]};{target[0]},{target[2]},,push:500@{first}'
    command = [
        nmo.executable,
        'behavior',
        'run',
        '--physics',
        'ivp',
        '--write',
        str(out),
        '--path',
        str(game),
        '-n',
        str(frames),
        '--steer',
        steer,
        '--steer-object',
        'Ball_Wood,Ball_Stone,Ball_Paper',
        '--steer-log',
        str(out / 'steer.csv'),
        '--physics-log',
        str(out / 'physics.csv'),
        str(base),
    ]
    with (out / 'runtime.log').open('w') as log:
        subprocess.run(command, cwd=game, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out / 'steer.csv').open() as stream:
        first_pose = next(csv.DictReader(stream), None)
    spawn_observed = bool(
        first_pose
        and first_pose['object_name'] == f'Ball_{ball}'
        and math.dist([float(first_pose[k]) for k in ('x', 'y', 'z')], start) < 3
    )
    trace = motion_trace.inspect(out / 'physics.csv', layout)
    bodies = [b for b in trace['bodies'] if b['mechanism'] == instance['id']]
    recipe = motions.catalog()[kind]
    required = {n for n, c in recipe['contracts'].items() if c['physical'] and c['type'] != 'fixed'}
    missing = sorted(required - {b['body'] for b in bodies})
    displaced = [b['body'] for b in bodies if b['first_displaced_frame'] is not None]
    split_observed = name != 'Plates' or any(
        s['mechanism'] == instance['id'] for s in trace['observed_states']
    )
    original_unchanged = digest(source) == before
    template_matches = digest(game / recipe['template']) == recipe['sha256']
    report = {
        'resource': name,
        'kind': kind,
        'source': str(source),
        'source_sha256': before,
        'original_unchanged': original_unchanged,
        'level_copy_identical': digest(level) == before,
        'template_matches': template_matches,
        'harness_sha256': digest(base),
        'nmo_sha256': digest(Path(nmo.executable)),
        'operation_types_sha256': digest(harness.DATA_DIR / 'virtools_operation_types.json'),
        'probe_level_sha256': digest(level),
        'modified_markers': [],
        'probe_placement_at_ms': 6000 + harness.setup_ms(sector),
        'spawn': start,
        'target': target,
        'ball_physics': physics,
        'ball_template_sha256': digest(ball_file),
        'ball_radius': 2,
        'ball': ball,
        'frames': frames,
        'spawn_observed': spawn_observed,
        'required_state_observed': split_observed,
        'instance': instance,
        'missing_bodies': missing,
        'displaced_bodies': displaced,
        'trace': trace,
        'status': 'passed'
        if spawn_observed
        and split_observed
        and original_unchanged
        and digest(level) == before
        and template_matches
        and not missing
        and displaced
        and trace['status'] == 'passed'
        else 'failed',
        'scope': 'Local contact probe in a byte-identical original-level copy. The '
        'separate harness places the ball '
        'after sector activation and recreates its sphere from native '
        'ball-table parameters. Movement and sampled containment only, '
        'not full-level completion.',
    }
    if recovery:
        from .recovery import plates

        reset = next(
            (
                o
                for o in nmo.objects(source, 'CK3dEntity')
                if o['name'] == f'PR_Resetpoint_{sector:02d}'
            ),
            None,
        )
        if reset is None:
            raise ValueError('Original checkpoint reset marker missing')
        reset_position = matrix_of(field_values(reset)['world_matrix'])[12:15]
        report['recovery'] = plates(
            out / 'physics.csv', out / 'runtime.log', trace, instance, recipe, reset_position, ball
        )
        if report['recovery']['status'] != 'passed':
            report['status'] = 'failed'
    write_json(out / 'validation.json', report)
    return report
