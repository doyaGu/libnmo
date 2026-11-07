"""Compare authored shortcuts with the normal route in the same native level.

Only steering inputs change. Runs start at the level's native start, retain
checkpoint and mechanism contracts, and measure continuous departures and
landings. Optional rewards may be skipped; this is not an all-rewards category.
"""

import json
import math
import re
import shutil
import subprocess
from pathlib import Path

from ..core import evidence, project
from ..core.geometry import at
from ..core.path import number
from ..route.dynamics import copy_runtime
from ..route.layout import Layout
from ..route.motions import template_paths
from . import gameplay, harness, landings
from .actions import ActionTrace
from .play import BALLS, FRAME_MS, base_for, read_run, steer_from, steer_text
from .resources import check as resource_check, dependency_paths


def position(reference, layout, dimensions):
    if not isinstance(reference, dict) or set(reference) != {'owner', 'at'}:
        raise ValueError('a shortcut position needs owner and at')
    owner = reference['owner']
    if not isinstance(owner, str) or owner not in layout.entry_poses:
        raise ValueError('shortcut owner must resolve to a route entry')
    values = reference['at']
    if not isinstance(values, list) or len(values) != dimensions:
        raise ValueError(f'shortcut position needs {dimensions} local coordinates')
    local = [number(v, 'shortcut coordinate') for v in values]
    pose = layout.entry_poses[owner]
    x, y, z = pose['position']
    px, pz = at((x, z), pose['heading'], *local[:2])
    return [px, y + local[2], pz] if dimensions == 3 else [px, pz]


def anchor(spec, layout, sectors):
    if not isinstance(spec, dict) or set(spec) - {'owner', 'at', 'occurrence', 'advance'}:
        raise ValueError('unknown shortcut anchor fields')
    point = position({k: spec.get(k) for k in ('owner', 'at')}, layout, 2)
    sector = sectors[spec['owner']]
    start = layout.sector_starts[sector - 1]['point']
    end = (
        layout.sector_starts[sector]['point']
        if sector < len(layout.sectors)
        else len(layout.points)
    )
    matches = [i for i in range(start, end) if math.dist(layout.points[i][:2], point) < 1e-5]
    occurrence = spec.get('occurrence')
    if occurrence is None:
        if len(matches) != 1:
            raise ValueError('shortcut anchor must be unique or specify occurrence')
        occurrence = 0
    if type(occurrence) is not int or not 0 <= occurrence < len(matches):
        raise ValueError('shortcut anchor occurrence does not exist')
    advance = spec.get('advance', 0)
    if type(advance) is not int or not 0 <= advance <= 8 or matches[occurrence] + advance >= end:
        raise ValueError('shortcut anchor advance must remain in its sector')
    return matches[occurrence] + advance, sector


def compile_shortcuts(spec, layout):
    if (
        not isinstance(spec, dict)
        or set(spec) != {'version', 'shortcuts'}
        or type(spec['version']) is not int
        or spec['version'] != 1
    ):
        raise ValueError('shortcut specification needs version 1 and shortcuts')
    values = spec['shortcuts']
    if not isinstance(values, list) or not 1 <= len(values) <= 8:
        raise ValueError('provide 1..8 authored shortcuts')
    sectors = {p.origin.entity_id: p.sector for p in layout.parts if p.origin}
    result, identifiers = [], set()
    for item in values:
        if not isinstance(item, dict) or set(item) != {
            'id',
            'leave_after',
            'resume_before',
            'waypoints',
            'departure',
            'landing',
            'landing_radius',
            'ball',
            'minimum_saved_frames',
            'tradeoff',
        }:
            raise ValueError('shortcut fields are incomplete or unknown')
        identifier = item['id']
        if (
            not isinstance(identifier, str)
            or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_.-]*', identifier)
            or identifier in identifiers
            or identifier in ('normal', 'combined')
        ):
            raise ValueError('shortcuts need unique stable IDs')
        identifiers.add(identifier)
        start, sector = anchor(item['leave_after'], layout, sectors)
        end, finish_sector = anchor(item['resume_before'], layout, sectors)
        if start >= end or sector != finish_sector:
            raise ValueError('a shortcut must advance within one sector, retaining checkpoints')
        waypoints = item['waypoints']
        if not isinstance(waypoints, list) or not 1 <= len(waypoints) <= 16:
            raise ValueError('shortcut needs 1..16 steering waypoints')
        points = []
        for waypoint in waypoints:
            if (
                not isinstance(waypoint, dict)
                or set(waypoint) - {'owner', 'at', 'hold', 'drive'}
                or ('hold' in waypoint and 'drive' in waypoint)
            ):
                raise ValueError('a waypoint accepts owner, at, and hold or drive frames')
            point = position({k: waypoint.get(k) for k in ('owner', 'at')}, layout, 2)
            if sectors[waypoint['owner']] != sector:
                raise ValueError('shortcut waypoints must stay in the same sector')
            count = waypoint.get('hold', waypoint.get('drive', 0))
            if (
                type(count) is not int
                or not 0 <= count <= 600
                or ('drive' in waypoint and count == 0)
            ):
                raise ValueError('hold needs 0..600 frames; drive needs 1..600')
            # A drive is a finite native key interval, not a target height.
            points.append(
                [*point, 1000000, f'push:{count}'] if 'drive' in waypoint else [*point, None, count]
            )
        departure = position(item['departure'], layout, 3)
        landing = position(item['landing'], layout, 3)
        if (
            any(sectors[item[key]['owner']] != sector for key in ('departure', 'landing'))
            or landing[1] >= departure[1] - 1
        ):
            raise ValueError('shortcut landing must descend within its starting sector')
        radius = number(item['landing_radius'], 'landing radius')
        saving = item['minimum_saved_frames']
        if not 0.5 <= radius <= 4 or type(saving) is not int or not 1 <= saving <= 12000:
            raise ValueError('invalid landing radius or minimum time saving')
        if (
            item['ball'] not in ('Wood', 'Stone', 'Paper')
            or not isinstance(item['tradeoff'], str)
            or not item['tradeoff'].strip()
        ):
            raise ValueError('shortcut needs its ball and a tradeoff description')
        result.append(
            {
                **item,
                'start': start,
                'end': end,
                'sector': sector,
                'points': points,
                'passage': {'points': [departure, landing], 'landing_radius': radius},
            }
        )
    result.sort(key=lambda cut: cut['start'])
    if any(a['end'] > b['start'] for a, b in zip(result, result[1:])):
        raise ValueError('shortcut ranges overlap')
    return result


def route_points(points, cuts):
    result, offset = [], 0
    for cut in sorted(cuts, key=lambda cut: cut['start']):
        result.extend(points[offset : cut['start'] + 1])
        result.extend(cut['points'])
        offset = cut['end']
    return [*result, *points[offset:]]


def run_shortcuts(source, specification, out, nmo, game):
    source, specification, out, game = [
        Path(p).resolve() for p in (source, specification, out, game)
    ]
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / 'shortcuts.json'
    project.write_json(
        report_path, {'status': 'failed', 'reason': 'Shortcut verification incomplete.'}
    )
    Path(str(report_path) + '.evidence.json').unlink(missing_ok=True)
    if (
        project.inspect(source)['status'] != 'passed'
        or resource_check(source, game)['status'] == 'failed'
    ):
        raise ValueError('shortcut source or native resources are stale; rebuild first')
    layout = Layout(json.loads((source / 'route.json').read_text()))
    cuts = compile_shortcuts(json.loads(specification.read_text()), layout)
    level = project.pack(source, nmo)
    base = base_for(source, nmo, 1)
    contracts = json.loads((source / 'gameplay_contracts.json').read_text())
    dependencies = [
        specification,
        Path(nmo.executable),
        base,
        *template_paths(game),
        *dependency_paths(source, game),
        *[game / '3D Entities' / name for name, _ in harness.MAIN_FILES],
    ]
    provenance = evidence.capture(
        source,
        'speedrun-shortcuts',
        {'frames': 24000, 'frame_ms': FRAME_MS, 'seed': 1},
        dependencies,
    )
    profiles = [('normal', []), *[(c['id'], [c]) for c in cuts], ('combined', cuts)]
    results = []
    for identifier, selected in profiles:
        folder = out / identifier
        folder.mkdir(exist_ok=True)
        overlay = folder / '3D Entities'
        if overlay.exists():
            shutil.rmtree(overlay)
        for name in ('steer.csv', 'physics.csv', 'validation.json'):
            (folder / name).unlink(missing_ok=True)
        shutil.copyfile(level, folder / 'Level_01.NMO')
        shutil.copyfile(source / 'manifest.json', folder / 'manifest.json')
        copy_runtime(source, folder)
        points = route_points(layout.points, selected)
        project.write_json(folder / 'route_points.json', points)
        command = [
            nmo.executable,
            'behavior',
            'run',
            '--physics',
            'ivp',
            '--delta',
            FRAME_MS,
            '--seed',
            '1',
            '--write',
            str(folder),
            '--path',
            str(game),
            '-n',
            '24000',
            '--steer',
            f'{steer_text(points)}@{steer_from(1)}',
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
                'command': command,
                'shortcuts': selected,
                'level_sha256': project.digest(level),
                'harness_sha256': project.digest(base),
            },
        )
        with (folder / 'run.log').open('w') as log:
            subprocess.run(
                command, cwd=game, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240
            )
        play = read_run(folder, 24000)
        gameplay_report = gameplay.inspect(folder, contracts, finish=play.finished)
        arrivals = []
        rows = [
            r
            for r in ActionTrace(folder, []).ball
            if play.finished is None or r['frame'] <= play.finished
        ]
        for cut in selected:
            landing = landings.observe(
                cut['passage'], [r for r in rows if r['name'] == 'Ball_' + cut['ball']]
            )
            arrivals.append({'id': cut['id'], **landing})
        checkpoints = [
            f
            for f, event in play.events
            if event == 'Checkpoint reached'
            and f >= steer_from(1)
            and (play.finished is None or f < play.finished)
        ]
        saving = (
            results[0]['finish_frame'] - play.finished
            if results and results[0]['finish_frame'] is not None and play.finished is not None
            else 0
        )
        passed = (
            play.finished is not None
            and not play.falls
            and len(checkpoints) == len(layout.sectors) - 1
            and gameplay_report['status'] == 'passed'
            and all(a['status'] == 'passed' for a in arrivals)
        )
        if selected:
            passed = passed and saving >= sum(c['minimum_saved_frames'] for c in selected)
        else:
            passed = passed and not play.missed
        result = {
            'id': identifier,
            'status': 'passed' if passed else 'failed',
            'finish_frame': play.finished,
            'saved_frames': saving,
            'saved_seconds': saving * float(FRAME_MS) / 1000,
            'checkpoints': checkpoints,
            'falls': play.falls,
            'gameplay': gameplay_report,
            'landings': arrivals,
            'missed_optional_rewards': play.missed,
            'tradeoffs': [c['tradeoff'] for c in selected],
        }
        project.write_json(folder / 'validation.json', result)
        for name in (
            'input.json',
            'route_points.json',
            'steer.csv',
            'physics.csv',
            'run.log',
            'validation.json',
        ):
            evidence.attach(folder / name, provenance)
        results.append(result)
        print(
            f'{identifier}: {result["status"]}, finish {play.finished}, saved {saving} frames',
            flush=True,
        )
    report = {
        'status': 'passed' if all(r['status'] == 'passed' for r in results) else 'failed',
        'profiles': results,
        'scope': 'Same native level, start, seed and fixed frame time; steering alone changes. '
        'No probe placement, impulse or mechanism injection. Six checkpoint contracts and '
        'all authored mechanism conditions remain mandatory. Timings describe these reference '
        'inputs, not an optimal run or human success probability. Optional rewards may be skipped.',
    }
    project.write_json(report_path, report)
    evidence.attach(report_path, provenance)
    return report
