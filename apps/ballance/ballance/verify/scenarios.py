"""Replay bounded local gameplay in an intact generated level and native runtime.

Runs start at a native checkpoint or supply one initial local probe placement.
Death, checkpoint respawn, mechanism reset and retry occur in the same world.
The native autopilot's input and full route prefix are retained for replay.
"""

import json
import math
import re
import shutil
import subprocess
from pathlib import Path

from ..core import evidence, project
from ..core.motion import place
from ..core.path import vector
from ..game.measures import FIRST_RESET_ABOVE, RESET_ABOVE
from ..route.actions import compile_action
from ..route.dynamics import copy_runtime
from ..route.layout import Layout
from ..route.motions import instances, template_paths
from . import harness
from .actions import ActionTrace, rotation_degrees
from .play import FRAME_MS, BALLS, SENT, steer_text
from .resources import check as resource_check, dependency_paths


def compile_scenarios(specification, layout):
    if (
        not isinstance(specification, dict)
        or set(specification) != {'version', 'scenarios'}
        or type(specification['version']) is not int
        or specification['version'] != 1
    ):
        raise ValueError('scenario suite needs version 1 and scenarios')
    if not isinstance(specification['scenarios'], list) or not specification['scenarios']:
        raise ValueError('scenario suite must not be empty')
    mechanisms = {item['id']: item for item in instances(layout)}
    parts = {part.origin.entity_id: part for part in layout.parts if part.origin}
    result = []
    identifiers = set()
    for spec in specification['scenarios']:
        if not isinstance(spec, dict) or set(spec) - {
            'id',
            'mechanism',
            'ball',
            'spawn',
            'route_to',
            'points',
            'exit',
            'frames',
            'expect',
            'conditions',
            'guard',
            'start',
        }:
            raise ValueError('unknown scenario fields')
        identifier = spec.get('id')
        if (
            not isinstance(identifier, str)
            or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_.-]*', identifier)
            or identifier in identifiers
        ):
            raise ValueError('scenario needs a unique stable id')
        identifiers.add(identifier)
        if not isinstance(spec.get('mechanism'), str):
            raise ValueError(f'{identifier}: mechanism needs a stable instance ID')
        item = mechanisms.get(spec.get('mechanism'))
        if item is None:
            raise ValueError(f'{identifier}: unknown mechanism')
        if spec.get('ball') not in ('Wood', 'Stone', 'Paper') or spec.get('expect') not in (
            'complete',
            'blocked',
            'recover',
        ):
            raise ValueError(f'{identifier}: invalid ball or expected outcome')
        frames = spec.get('frames')
        if type(frames) is not int or not 600 <= frames <= 24000:
            raise ValueError(f'{identifier}: frames must be 600..24000')
        if ('route_to' in spec) == ('points' in spec):
            raise ValueError(f'{identifier}: choose route_to or local points')
        conditions = spec.get('conditions')
        if not isinstance(conditions, list) or not conditions:
            raise ValueError(f'{identifier}: conditions must not be empty')
        conditions = [compile_action(value, layout) for value in conditions]
        guard = compile_action(spec.get('guard'), layout)
        if any(action['sector'] != item['sector'] for action in [*conditions, guard]):
            raise ValueError(f'{identifier}: all conditions must be in the starting sector')
        start_mode = spec.get('start', 'probe')
        if start_mode not in ('probe', 'checkpoint'):
            raise ValueError(f'{identifier}: start must be probe or checkpoint')
        checkpoint_ball = layout.sector_starts[item['sector'] - 1]['ball']
        if spec['expect'] == 'recover' and spec['ball'] != checkpoint_ball:
            raise ValueError(
                f'{identifier}: recovery must preserve the checkpoint ball; '
                'use a checkpoint start and its native changer route'
            )
        if start_mode == 'checkpoint':
            if 'spawn' in spec or spec['ball'] != checkpoint_ball:
                raise ValueError(
                    f'{identifier}: checkpoint start must retain its native ball and position'
                )
            if item['sector'] == 1:
                spawn = [layout.start[0], layout.marker_y + FIRST_RESET_ABOVE, layout.start[1]]
            else:
                marker = layout.checkpoints[item['sector'] - 2][0]
                spawn = [marker[0], marker[1] + RESET_ABOVE, marker[2]]
        else:
            spawn = place(
                vector(spec.get('spawn'), 'scenario spawn'), item['position'], item['yaw']
            )
        if 'route_to' in spec:
            if not isinstance(spec['route_to'], str):
                raise ValueError(f'{identifier}: route_to needs a stable entity ID')
            part = parts.get(spec['route_to'])
            if part is None or part.sector != item['sector']:
                raise ValueError(f'{identifier}: route_to must resolve in the starting sector')
            start = layout.sector_starts[item['sector'] - 1]['point']
            end = (
                layout.sector_starts[item['sector']]['point']
                if item['sector'] < len(layout.sectors)
                else len(layout.points)
            )
            matches = [
                i for i in range(start, end) if math.dist(layout.points[i][:2], part.end[0]) < 1e-6
            ]
            if len(matches) != 1:
                raise ValueError(
                    f'{identifier}: route endpoint must identify exactly one steering point'
                )
            points = layout.points[start : matches[0] + 1]
        else:
            if not isinstance(spec['points'], list) or not spec['points']:
                raise ValueError(f'{identifier}: points must not be empty')
            world = [
                place(vector(p, 'scenario point'), item['position'], item['yaw'])
                for p in spec['points']
            ]
            points = [[p[0], p[2]] for p in world]
        if spec['expect'] == 'recover' and ('route_to' not in spec or 'exit' not in spec):
            raise ValueError(
                f'{identifier}: recovery needs the checkpoint route prefix and a fall exit'
            )
        if 'exit' in spec:
            exit_position = place(
                vector(spec['exit'], 'scenario exit'), item['position'], item['yaw']
            )
            points = [*points, [exit_position[0], exit_position[2]]]
        result.append(
            {
                **spec,
                'instance': item,
                'sector': item['sector'],
                'spawn_world': spawn,
                'start': start_mode,
                'steering': points,
                'actions': conditions,
                'guard_action': guard,
            }
        )
    return result


def _native_ball_physics(nmo, game, ball):
    table = nmo.data(
        'data', 'dump', '--name', 'Physicalize_GameBall', str(game / '3D Entities/Balls.nmo')
    )
    row = next(row for row in table['rows'] if row[0] == f'Ball_{ball}')
    return {
        'Friction': row[1],
        'Elasticity': row[2],
        'Mass': row[3],
        'Collision Group': row[4],
        'Linear Speed Dampening': row[5],
        'Rot Speed Dampening': row[6],
        'Automatic Calculate Mass Center': False,
    }


def evaluate(folder, case, checkpoint):
    first = 370 + math.ceil(harness.setup_ms(case['sector']) / float(FRAME_MS))
    trace = ActionTrace(folder, [*case['actions'], case['guard_action']])
    deaths = []
    for line in (folder / 'runtime.log').read_text(errors='replace').splitlines():
        match = SENT.match(line)
        if match and match[2] == 'Ball Off' and int(match[1]) >= first:
            deaths.append(int(match[1]))
    last = case['frames'] - 1
    initial_end = min(deaths) - 1 if deaths else last
    first_pose = next((row for row in trace.ball if row['frame'] >= first), None)
    spawn_ok = bool(
        first_pose
        and first_pose['name'] == 'Ball_' + case['ball']
        and math.dist(first_pose['position'], case['spawn_world']) < 3
    )
    guard = trace.inspect(case['guard_action'], first, initial_end)
    actions = [trace.inspect(action, first, initial_end) for action in case['actions']]
    complete = all(action['status'] == 'passed' for action in actions)
    complete_window = all(action['complete_window'] for action in actions)
    # Absence needs a complete window. A positive action needs its sustained
    # observed interval; native ball changers legitimately leave gaps elsewhere.
    covered = all(action['samples'] >= 30 for action in actions) and (
        case['expect'] != 'blocked' or complete_window
    )
    passed = spawn_ok and guard['status'] == 'passed' and covered
    passed = passed and (
        not any(a['status'] == 'passed' for a in actions)
        if case['expect'] == 'blocked'
        else complete
    )
    report = {
        'id': case['id'],
        'expected': case['expect'],
        'spawn_observed': spawn_ok,
        'evidence_complete': complete_window,
        'evidence_sufficient': covered,
        'guard': guard,
        'actions': actions,
        'death_frames': deaths,
        'first_frame': first,
        'last_frame': last,
    }
    if case['expect'] == 'recover':
        death = deaths[0] if deaths else None
        next_death = deaths[1] if len(deaths) > 1 else last + 1
        dying_ball = next(
            (row for row in reversed(trace.ball) if death is not None and row['frame'] <= death),
            None,
        )
        respawn = next(
            (
                row
                for row in trace.ball
                if death is not None
                and death < row['frame'] < min(death + 900, next_death)
                and math.dist(row['position'], checkpoint) < 3
                and dying_ball is not None
                and row['name'] == dying_ball['name']
            ),
            None,
        )
        reset = []
        retry = []
        if respawn:
            for action in case['actions']:
                if action['type'] == 'rotation':
                    streak = 0
                    previous = None
                    restored = None
                    for row in trace.samples(action):
                        frame = row['frame']
                        if not death < frame < min(death + 900, next_death):
                            continue
                        close = (
                            rotation_degrees(row['matrix'], action['reference']) < 3
                            and math.dist(row['matrix'][12:15], action['reference'][12:15]) < 0.15
                        )
                        streak = (
                            (streak + 1 if previous is not None and frame == previous + 1 else 1)
                            if close
                            else 0
                        )
                        previous = frame
                        if streak >= 30:
                            restored = frame
                            break
                    reset.append(
                        {
                            'mechanism': action['mechanism'],
                            'frame': restored,
                            'status': 'passed' if restored else 'failed',
                        }
                    )
                retry.append(trace.inspect(action, respawn['frame'] + 1, next_death - 1))
        recovered = bool(
            respawn
            and all(r['status'] == 'passed' for r in reset)
            and retry
            and all(r['status'] == 'passed' for r in retry)
        )
        report['recovery'] = {
            'status': 'passed' if recovered else 'failed',
            'death_frame': death,
            'respawn': respawn,
            'expected_ball': dying_ball['name'] if dying_ball else None,
            'reset': reset,
            'retry': retry,
            'scope': 'Same native world: death, checkpoint respawn, rest poses where applicable '
            'and repeated action. No mechanism pose injection or process restart.',
        }
        passed = passed and recovered
    report['status'] = 'passed' if passed else 'failed'
    return report


def run_suite(source, specification, out, nmo, game, selected=None):
    source, specification, out, game = [
        Path(p).resolve() for p in (source, specification, out, game)
    ]
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / 'scenarios.json'
    project.write_json(
        report_path, {'status': 'failed', 'reason': 'Scenario run has not completed.'}
    )
    Path(str(report_path) + '.evidence.json').unlink(missing_ok=True)
    if (
        project.inspect(source)['status'] != 'passed'
        or resource_check(source, game)['status'] == 'failed'
    ):
        raise ValueError('scenario source or native resources are stale; rebuild first')
    layout = Layout(json.loads((source / 'route.json').read_text()))
    cases = compile_scenarios(json.loads(specification.read_text()), layout)
    if selected is not None:
        cases = [case for case in cases if case['id'] == selected]
        if not cases:
            raise ValueError(f'unknown scenario: {selected}')
    level = project.pack(source, nmo)
    manifest = json.loads((source / 'manifest.json').read_text())
    markers = {
        obj['name']: obj['transform']['position']
        for scene in manifest['scenes']
        for obj in scene['objects']
        if obj['name'].startswith('PR_Resetpoint_')
    }
    dependencies = [
        specification,
        Path(nmo.executable),
        *template_paths(game),
        *dependency_paths(source, game),
    ]
    dependencies += [game / '3D Entities' / name for name, _ in harness.MAIN_FILES]
    provenance = evidence.capture(
        source, 'gameplay-scenarios', {'selected': selected, 'frame_ms': FRAME_MS}, dependencies
    )
    results = []
    for case in cases:
        folder = out / case['id']
        folder.mkdir(exist_ok=True)
        # A previous parameterized run may have supplied a private Levelinit
        # loader. Never let that overlay leak into a later unmodified case.
        runtime_overlay = folder / '3D Entities'
        if runtime_overlay.exists():
            shutil.rmtree(runtime_overlay)
        shutil.copyfile(level, folder / 'Level_01.NMO')
        copy_runtime(source, folder)
        for name in ('steer.csv', 'physics.csv', 'validation.json'):
            (folder / name).unlink(missing_ok=True)
        base = folder / 'probe.cmo'
        harness.write(
            base,
            nmo,
            case['sector'],
            case['ball'],
            probe_position=case['spawn_world'] if case['start'] == 'probe' else None,
            probe_physics=_native_ball_physics(nmo, game, case['ball'])
            if case['start'] == 'probe'
            else None,
        )
        first = 370 + math.ceil(harness.setup_ms(case['sector']) / float(FRAME_MS))
        command = [
            nmo.executable,
            'behavior',
            'run',
            '--physics',
            'ivp',
            '--delta',
            FRAME_MS,
            '--write',
            str(folder),
            '--path',
            str(game),
            '-n',
            str(case['frames']),
            '--steer',
            f'{steer_text(case["steering"])}@{first}',
            '--steer-object',
            BALLS,
            '--steer-log',
            str(folder / 'steer.csv'),
            '--physics-log',
            str(folder / 'physics.csv'),
            str(base),
        ]
        project.write_json(
            folder / 'input.json',
            {
                'case': case,
                'command': command,
                'level_sha256': project.digest(level),
                'harness_sha256': project.digest(base),
            },
        )
        with (folder / 'runtime.log').open('w') as log:
            subprocess.run(
                command, cwd=game, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180
            )
        result = evaluate(folder, case, markers[f'PR_Resetpoint_{case["sector"]:02d}'])
        project.write_json(folder / 'validation.json', result)
        for name in ('input.json', 'steer.csv', 'physics.csv', 'runtime.log', 'validation.json'):
            evidence.attach(folder / name, provenance)
        results.append(result)
    report = {
        'status': 'passed' if all(r['status'] == 'passed' for r in results) else 'failed',
        'scenarios': results,
        'scope': 'Bounded authored inputs in the generated level and native IVP runtime. '
        'Blocked cases prove absence only in their observed window; '
        'no exhaustive shortcut or soft-lock proof.',
    }
    project.write_json(report_path, report)
    evidence.attach(report_path, provenance)
    return report
