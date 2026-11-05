"""Observe a plate break, native death, restored bodies and checkpoint respawn.

This is a bounded recovery scenario, not a general soft-lock or joint proof.
"""
import csv
import math

from ..core.nmo import into, place as matrix_place
from ..core.motion import place
from .play import SENT


def plates(physics, log, trace, instance, recipe, reset_position, ball, deadline=600):
    splits = [s['first_frame'] for s in trace['observed_states'] if s['mechanism'] == instance['id']]
    deaths = []
    for line in log.read_text(encoding='utf-8', errors='replace').splitlines():
        match = SENT.match(line)
        if match and match[2] == 'Ball Off' and splits and int(match[1]) > min(splits):
            deaths.append(int(match[1]))
    result = {'status': 'failed', 'split_frame': min(splits) if splits else None,
              'death_frame': min(deaths) if deaths else None, 'restored_frame': None,
              'respawn_frame': None, 'deadline_frames': deadline, 'stable_frames_required': 30,
              'scope': 'Natural plate break and death, all nine body poses restored for 30 frames, '
                       'and the native ball observed near its checkpoint. Does not prove recreated joint topology or all recovery paths.'}
    if not deaths:
        result['reason'] = 'No native death observed after the plate split.'
        return result
    death = min(deaths)
    required = {name for name, c in recipe['contracts'].items() if c['physical'] and c['type'] != 'fixed'}
    ids = {identifier: b['body'] for b in trace['bodies'] if b['mechanism'] == instance['id']
           for identifier in b['runtime_ids']}
    restored = {};run = 0;previous = None
    expected = {name: [place(p, instance['position'], instance['yaw']) for p in recipe['bodies'][name]['points']]
                for name in required}
    local = {name: [into(recipe['bodies'][name]['matrix'], p) for p in recipe['bodies'][name]['points']]
             for name in required}
    def end_frame(frame):
        nonlocal run
        run = run + 1 if required <= restored.keys() and all(restored.values()) else 0
        if run >= 30 and result['restored_frame'] is None:
            result['restored_frame'] = frame - 29
    with physics.open(newline='') as stream:
        for row in csv.DictReader(stream):
            frame = int(row['frame'])
            if not death < frame <= death + deadline:
                continue
            if frame != previous:
                if previous is not None:
                    end_frame(previous)
                    if frame != previous + 1:
                        run = 0
                restored = {};previous = frame
            matrix = [float(row[f'm{k}']) for k in range(16)]
            if not all(math.isfinite(v) for v in matrix):
                raise ValueError('Non-finite recovery pose')
            name = ids.get(row['id'])
            if name in required:
                restored[name] = max(math.dist(matrix_place(matrix, p), q)
                                     for p, q in zip(local[name], expected[name])) <= .1
            if row['name'] == f'Ball_{ball}' and math.dist(matrix[12:15], reset_position) < 3:
                result['respawn_frame'] = result['respawn_frame'] or frame
    if previous is not None:
        end_frame(previous)
    result['restored_bodies'] = sorted(required) if result['restored_frame'] is not None else []
    result['status'] = 'passed' if result['restored_frame'] is not None and result['respawn_frame'] is not None else 'failed'
    return result
