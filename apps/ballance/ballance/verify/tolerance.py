"""Bounded perturbation experiments with fixed native inputs and honest coverage.

Speed is a native centre impulse (mass * speed), lateral offset is perpendicular
 to the authored entry heading, timing shifts the recorded key transitions, and
phase advances a native input prelude before releasing the test ball again.
The sampled success set is not an interpolated safe region or a human success rate.
"""

import copy
import csv
import itertools
import json
import math
import re
from pathlib import Path

from ..core import evidence, project
from ..core.path import number
from ..core.motion import place
from ..route.layout import Layout
from ..route.motions import template_paths, catalog
from . import harness
from .actions import ActionTrace, hinge_degrees
from .play import BALLS, FRAME_MS
from .replay import read_inputs, shift_inputs, death_frames
from .resources import check as resource_check, dependency_paths
from .scenarios import compile_scenarios, first_frame, run_case

AXES = ('entry_speed', 'lateral_offset', 'timing_frames', 'phase_frames')
LIMITS = ((0, 60), (-10, 10), (-120, 120), (0, 1200))


def parameter_values(value):
    if not isinstance(value, dict) or set(value) - set(AXES):
        raise ValueError('unknown perturbation axis')
    result = {}
    for axis, (low, high) in zip(AXES, LIMITS):
        v = number(value.get(axis, 0), axis)
        if not low <= v <= high or (axis.endswith('frames') and v != int(v)):
            raise ValueError(f'{axis} must be within {low}..{high}, with integral frames')
        result[axis] = int(v) if axis.endswith('frames') else v
    return result


def sample_parameters(spec):
    axes = spec.get('axes')
    if not isinstance(axes, dict) or set(axes) != set(AXES):
        raise ValueError('tolerance experiment needs all four perturbation axes')
    for axis, values in axes.items():
        if not isinstance(values, list) or not 1 <= len(values) <= 16:
            raise ValueError('each axis needs 1..16 explicit values')
        for value in values:
            parameter_values({axis: value})
        if 0 not in values or len(set(values)) != len(values):
            raise ValueError('each axis needs zero and distinct values')
    mode = spec.get('sampling', 'axes')
    if mode not in ('axes', 'product'):
        raise ValueError('sampling must be axes or product')
    values = [parameter_values({})]
    if mode == 'product':
        if math.prod(len(v) for v in axes.values()) > 256:
            raise ValueError('Cartesian grid is limited to 256 samples per experiment')
        values += [dict(zip(AXES, row)) for row in itertools.product(*(axes[k] for k in AXES))]
    else:
        values += [parameter_values({axis: v}) for axis in AXES for v in axes[axis]]
    combinations = spec.get('combinations', [])
    if not isinstance(combinations, list) or len(combinations) > 32:
        raise ValueError('combinations must be a list of at most 32 samples')
    values += [parameter_values(value) for value in combinations]
    result = []
    for value in values:
        normalized = parameter_values(value)
        if normalized not in result:
            result.append(normalized)
    return result


def landing_case(spec, layout):
    passage = next((p for p in layout.passages if p['id'] == spec['landing']), None)
    if passage is None or passage['kind'] != 'landing':
        raise ValueError('landing experiment must reference a declared landing passage')
    part = next(p for p in layout.parts if p.origin and p.origin.entity_id == passage['owner'])
    pose = layout.entry_poses[passage['owner']]
    dx, dz = pose['heading']
    # Start on the preceding route, before the departure box. Preserve the
    # authored steering path through the drop rather than drawing a shortcut.
    x, y, z = pose['position']
    spawn = [x - 8 * dx, y + 2, z - 8 * dz]
    start = layout.sector_starts[part.sector - 1]['point']
    end = (
        layout.sector_starts[part.sector]['point']
        if part.sector < len(layout.sectors)
        else len(layout.points)
    )
    points = layout.points[start:end]
    landing_xz = [passage['points'][-1][k] for k in (0, 2)]
    nearest = min(range(len(points)), key=lambda i: math.dist(points[i][:2], landing_xz))
    case = {
        'id': spec['id'],
        'sector': part.sector,
        'ball': spec.get('ball', 'Wood'),
        'spawn_world': spawn,
        'start': 'probe',
        'frames': 1800,
        'expect': 'complete',
        'steering': points[: nearest + 2],
        'landing': passage,
    }
    case['frames'] += first_frame(case)
    case['first_frame'] = first_frame(case)
    return case


def compile_experiments(specification, scenarios, layout):
    if (
        not isinstance(specification, dict)
        or set(specification) != {'version', 'suite', 'experiments'}
        or type(specification['version']) is not int
        or specification['version'] != 1
    ):
        raise ValueError('tolerance suite needs version 1, suite and experiments')
    values = specification['experiments']
    if not isinstance(values, list) or not 1 <= len(values) <= 32:
        raise ValueError('tolerance suite needs 1..32 experiments')
    cases = {case['id']: case for case in compile_scenarios(scenarios, layout)}
    result, identifiers = [], set()
    for spec in values:
        if not isinstance(spec, dict) or set(spec) - {
            'id',
            'scenario',
            'landing',
            'ball',
            'heading',
            'axes',
            'sampling',
            'combinations',
        }:
            raise ValueError('unknown tolerance experiment fields')
        identifier = spec.get('id')
        if (
            not isinstance(identifier, str)
            or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_.-]*', identifier)
            or identifier in identifiers
        ):
            raise ValueError('experiments need unique stable IDs')
        identifiers.add(identifier)
        if ('scenario' in spec) == ('landing' in spec):
            raise ValueError('choose a scenario or a landing passage')
        if 'landing' in spec:
            case = landing_case(spec, layout)
        else:
            if not isinstance(spec['scenario'], str) or spec['scenario'] not in cases:
                raise ValueError('unknown reference scenario')
            case = copy.deepcopy(cases[spec['scenario']])
        if case['start'] != 'probe' or case['expect'] != 'complete':
            raise ValueError('tolerance references require a positive probe scenario')
        if case['ball'] not in ('Wood', 'Stone', 'Paper'):
            raise ValueError('unknown probe ball')
        heading = spec.get('heading')
        if not isinstance(heading, list) or len(heading) != 2:
            raise ValueError('entry heading needs a world-space XZ vector')
        heading = [number(v, 'entry heading') for v in heading]
        length = math.hypot(*heading)
        if length < 1e-6:
            raise ValueError('entry heading must be nonzero')
        samples = sample_parameters(spec)
        if 'landing' in case and any(p['phase_frames'] for p in samples):
            raise ValueError('a static landing has no mechanism phase to perturb')
        if any(
            catalog()[item['kind']]['type'] not in ('fan', 'hinge')
            for item in target_instances(case).values()
        ):
            raise ValueError('phase observations currently require fan or hinge mechanisms')
        result.append(
            {
                'id': identifier,
                'case': case,
                'heading': [v / length for v in heading],
                'samples': samples,
            }
        )
    return result


def perturb_case(experiment, parameters, index):
    case = copy.deepcopy(experiment['case'])
    first = first_frame(case)
    phase = parameters['phase_frames']
    dx, dz = experiment['heading']
    offset = parameters['lateral_offset']
    initial = list(case['spawn_world'])
    case['spawn_world'] = list(initial)
    case['spawn_world'][0] += dz * offset
    case['spawn_world'][2] -= dx * offset
    case['id'] = f'{experiment["id"]}-{index:03d}'
    case['first_frame'] = first + phase
    case['frames'] += phase
    speed = parameters['entry_speed']
    case['probe_options'] = {
        'probe_velocity': [dx * speed, 0, dz * speed],
        'probe_delay_frames': phase,
    }
    if phase:
        case['probe_options']['probe_rehearsal_position'] = initial
    names = {
        catalog()[instance['kind']]['body'] + '*' for instance in target_instances(case).values()
    }
    case['observe'] = ','.join([BALLS, *sorted(names)])
    case['parameters'] = parameters
    return case


def target_instances(case):
    instances = {}
    for action in case.get('actions', []):
        instances[action['instance']['id']] = action['instance']
    if 'instance' in case:
        instances[case['instance']['id']] = case['instance']
    return instances


def release_states(folder, case):
    """Bind at the unperturbed rest location, then follow the same runtime ID."""
    snapshots = json.loads((folder / 'observations.json').read_text())
    first = next(
        s for s in snapshots if s['frame'] == first_frame(case) - case['parameters']['phase_frames']
    )
    released = next(s for s in snapshots if s['frame'] == first_frame(case))
    instances = target_instances(case)
    result = []
    for instance in instances.values():
        recipe = catalog()[instance['kind']]
        body_names = [recipe['body']]
        for name in body_names:
            matrix = recipe['bodies'][name]['matrix']
            centre = place(matrix[12:15], instance['position'], instance['yaw'])
            matches = [
                e
                for e in first['entities']
                if re.fullmatch(re.escape(name) + r'\d*', e['name'])
                and math.dist(e['matrix'][12:15], centre) < 1
            ]
            if len(matches) != 1:
                raise ValueError(f'cannot bind release observation: {instance["id"]}/{name}')
            observed = next((e for e in released['entities'] if e['id'] == matches[0]['id']), None)
            if observed is None:
                raise ValueError('native release pose is missing')
            reference = []
            for offset in (0, 4, 8):
                reference.extend(
                    [*place(matrix[offset : offset + 3], (0, 0, 0), instance['yaw']), 0]
                )
            reference.extend([*centre, 1])
            axis = place(recipe['axis'], (0, 0, 0), instance['yaw'])
            result.append(
                {
                    'mechanism': instance['id'],
                    'body': name,
                    'runtime_id': observed['id'],
                    'frame': released['frame'],
                    'degrees': hinge_degrees(observed['matrix'], reference, axis),
                    'matrix': observed['matrix'],
                }
            )
    return result


def completion_frame(result):
    if 'landing' in result:
        return result['landing']['confirmed_frame']
    observations = [result['guard'], *result['actions']]
    return max(item['observed']['frame'] for item in observations)


def measure(folder, case, result, checkpoint):
    rows = [r for r in ActionTrace(folder, []).ball if r['frame'] >= first_frame(case)]
    speed = None
    if len(rows) >= 2 and rows[1]['frame'] == rows[0]['frame'] + 1:
        a, b = rows[0]['position'], rows[1]['position']
        speed = math.hypot(b[0] - a[0], b[2] - a[2]) * 1000 / float(FRAME_MS)
    deaths = result.get('death_frames', [])
    prelude_deaths = [
        f
        for f in death_frames(folder, first_frame(case) - case['parameters']['phase_frames'])
        if f < first_frame(case)
    ]
    if (
        not result['spawn_observed']
        or prelude_deaths
        or (not result['evidence_sufficient'] and not deaths)
    ):
        outcome = 'invalid'
    elif result['status'] == 'passed':
        outcome = 'completed_then_died' if deaths else 'success'
    elif deaths:
        outcome = 'death_before_completion'
    elif result.get('guard', {}).get('status') == 'failed':
        outcome = 'approach_missed'
    else:
        outcome = 'condition_not_met'
    dying_ball = next((r for r in reversed(rows) if deaths and r['frame'] <= deaths[0]), None)
    respawn = next(
        (
            r['frame']
            for r in rows
            if deaths
            and dying_ball is not None
            and r['name'] == dying_ball['name']
            and r['frame'] > deaths[0]
            and math.dist(r['position'], checkpoint) < 3
        ),
        None,
    )
    return {
        'id': case['id'],
        'parameters': case['parameters'],
        'outcome': outcome,
        'goal_completed': result['status'] == 'passed',
        'observed_entry_speed': speed,
        'release_states': release_states(folder, case),
        'completion_frames': completion_frame(result) - first_frame(case)
        if result['status'] == 'passed'
        else None,
        'first_death_frame': deaths[0] if deaths else None,
        'prelude_death_frames': prelude_deaths,
        'respawn_frames': respawn - deaths[0] if respawn is not None else None,
        'validation': str(folder / 'validation.json'),
        'input': str(folder / 'input.json'),
        'keys': str(folder / 'keys.json'),
        'release_observation': str(folder / 'observations.json'),
    }


def summarize(experiment, samples):
    baseline = samples[0]
    axes = {}
    for axis in AXES:
        isolated = [r for r in samples if all(r['parameters'][k] == 0 for k in AXES if k != axis)]
        axes[axis] = {
            outcome: sorted(r['parameters'][axis] for r in isolated if r['outcome'] == outcome)
            for outcome in sorted({r['outcome'] for r in isolated})
        }
    valid = [r for r in samples if r['outcome'] != 'invalid']
    succeeded = sum(r['outcome'] == 'success' for r in valid)
    return {
        'id': experiment['id'],
        'status': 'failed'
        if baseline['outcome'] != 'success' or len(valid) != len(samples)
        else 'passed'
        if succeeded == len(samples)
        else 'review',
        'sample_count': len(samples),
        'valid_samples': len(valid),
        'successful_samples': succeeded,
        'goal_completions': sum(r.get('goal_completed', r['outcome'] == 'success') for r in valid),
        'axes': axes,
        'samples': samples,
    }


def run_tolerance(source, specification, out, nmo, game, selected=None):
    source, specification, out, game = [
        Path(p).resolve() for p in (source, specification, out, game)
    ]
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / 'tolerance.json'
    project.write_json(report_path, {'status': 'failed', 'reason': 'Tolerance run incomplete.'})
    Path(str(report_path) + '.evidence.json').unlink(missing_ok=True)
    if (
        project.inspect(source)['status'] != 'passed'
        or resource_check(source, game)['status'] == 'failed'
    ):
        raise ValueError('tolerance source or native resources are stale; rebuild first')
    spec = json.loads(specification.read_text())
    if not isinstance(spec, dict) or not isinstance(spec.get('suite'), str):
        raise ValueError('tolerance suite needs a scenario suite path')
    suite = specification.parent / spec['suite']
    layout = Layout(json.loads((source / 'route.json').read_text()))
    experiments = compile_experiments(spec, json.loads(suite.read_text()), layout)
    if selected:
        experiments = [e for e in experiments if e['id'] == selected]
        if not experiments:
            raise ValueError(f'unknown experiment: {selected}')
    level = project.pack(source, nmo)
    manifest = json.loads((source / 'manifest.json').read_text())
    markers = {
        o['name']: o['transform']['position']
        for s in manifest['scenes']
        for o in s['objects']
        if o['name'].startswith('PR_Resetpoint_')
    }
    dependencies = [
        specification,
        suite,
        Path(nmo.executable),
        *template_paths(game),
        *dependency_paths(source, game),
        *[game / '3D Entities' / name for name, _ in harness.MAIN_FILES],
    ]
    provenance = evidence.capture(
        source,
        'gameplay-tolerance',
        {'selected': selected, 'frame_ms': FRAME_MS, 'seed': 1},
        dependencies,
    )
    results = []
    for experiment in experiments:
        base = experiment['case']
        checkpoint = markers[f'PR_Resetpoint_{base["sector"]:02d}']
        folder = out / (experiment['id'] + '-reference')
        reference = run_case(source, level, folder, base, nmo, game, checkpoint)
        project.write_json(folder / 'validation.json', reference)
        if reference['status'] != 'passed':
            raise ValueError(f'{experiment["id"]}: reference solution failed; see {folder}')
        inputs = read_inputs(folder / 'steer.csv')
        reference_physics = project.digest(folder / 'physics.csv')
        for name in ('input.json', 'steer.csv', 'physics.csv', 'runtime.log', 'validation.json'):
            evidence.attach(folder / name, provenance)
        samples = []
        for index, parameters in enumerate(experiment['samples']):
            case = perturb_case(experiment, parameters, index)
            folder = out / case['id']
            keys = shift_inputs(
                inputs,
                first_frame(base),
                case['frames'],
                parameters['timing_frames'],
                parameters['phase_frames'],
            )
            result = run_case(source, level, folder, case, nmo, game, checkpoint, keys)
            project.write_json(folder / 'validation.json', result)
            for name in (
                'input.json',
                'keys.json',
                'observations.json',
                'steer.csv',
                'physics.csv',
                'runtime.log',
                'validation.json',
            ):
                evidence.attach(folder / name, provenance)
            sample = measure(folder, case, result, checkpoint)
            if index == 0:
                sample['reference_physics_identical'] = (
                    project.digest(folder / 'physics.csv') == reference_physics
                )
                if not sample['reference_physics_identical']:
                    sample['outcome'] = 'invalid'
            samples.append(sample)
            print(f'{case["id"]}: {samples[-1]["outcome"]}', flush=True)
        results.append(summarize(experiment, samples))
    statuses = {r['status'] for r in results}
    report = {
        'status': 'failed'
        if 'failed' in statuses
        else 'review'
        if 'review' in statuses
        else 'passed',
        'experiments': results,
        'scope': 'Finite deterministic samples of fixed native keys, seed 1. No interpolation, '
        'human success probability, exhaustive safety or soft-lock proof. '
        'Entry speed is a mass-scaled native impulse; observed speed includes settling. '
        'Phase frames rehearse native inputs before releasing only the ball again; '
        'release observations record actual states, not requested angles. '
        'Respawn cost records checkpoint proximity, not a successful retry.',
    }
    project.write_json(report_path, report)
    evidence.attach(report_path, provenance)
    with (out / 'tolerance.csv').open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(
            ['experiment', 'sample', *AXES, 'outcome', 'observed_entry_speed', 'respawn_frames']
        )
        for experiment in results:
            for sample in experiment['samples']:
                writer.writerow(
                    [
                        experiment['id'],
                        sample['id'],
                        *[sample['parameters'][k] for k in AXES],
                        sample['outcome'],
                        sample['observed_entry_speed'],
                        sample['respawn_frames'],
                    ]
                )
    evidence.attach(out / 'tolerance.csv', provenance)
    return report
