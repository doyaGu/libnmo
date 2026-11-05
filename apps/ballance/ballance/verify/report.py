"""A reproducible verification report for a built project or original level.

Geometry findings stay visible for human review; no map-specific exceptions
or hard-coded test counts turn them into passes. A caller can make them
blocking with strict=True. Unperformed checks are explicitly skipped.
"""

from __future__ import annotations

from dataclasses import asdict
import json
from pathlib import Path

from ..core import project, evidence
from ..core.nmo import Nmo
from ..route.layout import Layout
from ..route import motions
from ..view.scene import load
from .clearance import check as clearance_check
from .geometry import check as geometry_check
from .scenery import check as scenery_check
from .motions import check as motion_check
from .motion_trace import inspect as motion_trace
from .gameplay import inspect as gameplay_check
from .resources import check as resource_check, dependency_paths
from .play import FRAME_MS, Play, play, steer_from


def _play_report(result: Play, folder: Path, sector: int) -> dict:
    finish = result.finished
    checkpoints = [
        frame
        for frame, event in result.events
        if event == 'Checkpoint reached'
        and frame >= steer_from(sector)
        and (finish is None or frame < finish)
    ]
    sectors_path = folder / 'route_sectors.json'
    expected = (
        len(json.loads(sectors_path.read_text(encoding='utf-8'))) - sector
        if sectors_path.is_file()
        else None
    )
    death_path = folder / 'death_zones.json'
    death = {'status': 'skipped', 'reason': 'No generated death-zone bounds.'}
    if death_path.is_file():
        boxes = json.loads(death_path.read_text(encoding='utf-8'))['boxes']
        near = [
            frame
            for frame, point in result.trace
            if any(
                all(box[k] - 4 <= v <= box[k + 1] + 4 for k, v in zip((0, 2, 4), point))
                for box in boxes
            )
        ]
        death = {
            'status': 'review' if near else 'passed',
            'count': len(boxes),
            'envelope_radius': 4,
            'intersecting_frames': near,
        }
    passed = (
        finish is not None
        and not result.falls
        and (expected is None or len(checkpoints) == expected)
    )
    return {
        'status': 'passed' if passed else 'failed',
        'sector': sector,
        'requested_frames': result.frames,
        'finished_frame': finish,
        'simulated_seconds': round(finish * float(FRAME_MS) / 1000, 3)
        if finish is not None
        else None,
        'falls_before_finish': result.falls,
        'checkpoints': checkpoints,
        'setup_checkpoints': [
            frame
            for frame, event in result.events
            if event == 'Checkpoint reached' and frame < steer_from(sector)
        ],
        'expected_checkpoints': expected,
        'stalls': [asdict(s) for s in result.stalls],
        'rewards': result.rewards,
        'missed_rewards': len(result.missed),
        'trace_samples': len(result.trace),
        'trace_height_range': [
            min(p[1] for _, p in result.trace),
            max(p[1] for _, p in result.trace),
        ],
        'death_zones': death,
    }


def verify(
    source: Path,
    nmo: Nmo,
    *,
    frames: int | None = None,
    game: Path | None = None,
    sector: int = 1,
    strict: bool = False,
) -> dict:
    """Run static checks and optional gameplay; return a JSON-safe report."""
    source = source.resolve()
    if sector < 1 or (frames is not None and frames <= 0):
        raise ValueError('frames and sector must be positive')
    if frames is not None and (not source.is_dir() or game is None):
        raise ValueError('--play needs a built level folder and --game')
    checks = {}
    provenance = evidence.capture(
        source,
        'verification',
        {
            'frames': frames,
            'sector': sector,
            'strict': strict,
            'frame_ms': FRAME_MS if frames is not None else None,
            'window': False,
            'game': str(game.resolve()) if game else None,
        },
        ([Path(nmo.executable)] if Path(nmo.executable).is_file() else [])
        + [
            p
            for p in [*motions.template_paths(game), *dependency_paths(source, game)]
            if p.is_file()
        ],
    )
    if source.is_dir():
        checks['build'] = project.inspect(source)
        checks['resources'] = resource_check(source, game)
    scene = load(source, nmo, hidden=True)
    findings = geometry_check(scene)
    checks['geometry'] = {
        'status': ('failed' if strict else 'review') if findings else 'passed',
        'findings': [asdict(f) for f in findings],
    }
    route_path = source / project.ROUTE_SNAPSHOT
    if source.is_dir() and route_path.is_file():
        layout = Layout(json.loads(route_path.read_text(encoding='utf-8')))
        from ..route.assets import Assets
        from ..route.motion_stops import prepare as prepare_motion_stops

        assets = Assets.existing(source)
        prepare_motion_stops(layout, assets)
        connections = layout.connection_report()
        checks['connections'] = {
            'status': 'failed' if connections.get('open_connections') else 'passed',
            'open_connections': connections.get('open_connections', []),
            'count': len(connections['connections']),
            'transitions': len(connections['transitions']),
            'scope': connections['scope'],
        }
        spatial_path = source / 'spatial_constraints.json'
        spatial = (
            json.loads(spatial_path.read_text(encoding='utf-8')) if spatial_path.is_file() else None
        )
        scenery = scenery_check(
            scene, layout, spatial['attachments'] if spatial and spatial['native_assets'] else None
        )
        placement_path = source / 'scenery_placement.json'
        placements = (
            json.loads(placement_path.read_text(encoding='utf-8'))
            if placement_path.is_file()
            else []
        )
        checks['scenery'] = {
            'status': 'failed'
            if scenery
            else 'review'
            if any(p['action'] == 'omitted' for p in placements)
            else 'passed',
            'findings': scenery,
            'placements': placements,
            'attachment_checks': len(spatial['attachments'])
            if spatial and spatial['native_assets']
            else None,
            'scope': spatial['scope']
            if spatial
            else 'Legacy clearance checks; no attachment record.',
        }
        # Read generated assets without copying or changing the build folder.
        checks['mechanisms'] = motion_check(layout, assets, game)
        checks['clearance'] = clearance_check(scene, layout)
    else:
        checks['clearance'] = {'status': 'skipped', 'reason': 'No builder route snapshot.'}
        checks['connections'] = {'status': 'skipped', 'reason': 'No builder route snapshot.'}
        checks['scenery'] = {'status': 'skipped', 'reason': 'No builder route snapshot.'}
        checks['mechanisms'] = {'status': 'skipped', 'reason': 'No builder route snapshot.'}
    checks['play'] = {'status': 'skipped', 'reason': 'Not requested.'}
    checks['gameplay'] = {'status': 'skipped', 'reason': 'No new gameplay trace requested.'}
    checks['mechanism_motion'] = {'status': 'skipped', 'reason': 'No new gameplay trace requested.'}
    if frames is not None:
        if (
            checks.get('connections', {}).get('status') == 'failed'
            or checks['build']['status'] == 'failed'
            or checks.get('resources', {}).get('status') == 'failed'
            or checks.get('mechanisms', {}).get('template_mismatches')
        ):
            checks['play'] = {
                'status': 'skipped',
                'reason': 'Build inputs or native resources changed; refresh and rebuild first.',
            }
        else:
            try:
                checks['play'] = _play_report(
                    play(source, frames, nmo, game, sector), source, sector
                )
                contract_path = source / 'gameplay_contracts.json'
                if contract_path.is_file():
                    checks['gameplay'] = gameplay_check(
                        source,
                        json.loads(contract_path.read_text()),
                        sector,
                        checks['play']['finished_frame'],
                    )
                if route_path.is_file():
                    checks['mechanism_motion'] = motion_trace(
                        source / 'physics.csv', layout, checks['play']['finished_frame']
                    )
                    if (source / 'physics.csv').is_file():
                        checks['mechanism_motion']['trace_sha256'] = project.digest(
                            source / 'physics.csv'
                        )
            except (OSError, RuntimeError, ValueError) as error:
                checks['play'] = {'status': 'failed', 'error': str(error)}
    statuses = [c['status'] for c in checks.values()]
    played = checks['play']
    review = (
        'review' in statuses
        or 'unavailable' in statuses
        or played.get('missed_rewards', 0) > 0
        or played.get('death_zones', {}).get('status') == 'review'
        or bool(checks.get('build', {}).get('warnings'))
        or checks.get('build', {}).get('native_assets') is False
    )
    status = 'failed' if 'failed' in statuses else 'review' if review else 'passed'
    return {
        'schema_version': 1,
        'source': str(source),
        'status': status,
        'checks': checks,
        'evidence': provenance,
        'limits': [
            'Geometry findings need inspection; original levels can contain intentional openings.',
            'Scenery uses conservative mesh bounds, rail/ball sweeps and a '
            '150-unit native balloon departure corridor.',
            'The departure corridor covers the visible standard balloon '
            'finish; it does not model UFO endings or unlimited flight.',
            'Gameplay uses the libnmo IVP harness; it is not a manual difficulty or visual review.',
            'Evidence tracks generator sources and CLI; full game resource '
            'dependency discovery is not yet implemented.',
            'Reward checks measure proximity, not score changes.',
            'Death-zone checks use a conservative four-unit envelope around '
            'sampled ball positions.',
        ],
    }


def write(report: dict, path: Path) -> None:
    project.write_json(path, report)
    if 'evidence' in report:
        evidence.attach(path, report['evidence'])
    else:
        Path(str(path) + '.evidence.json').unlink(missing_ok=True)


def summary(report: dict) -> list[str]:
    lines = []
    for name, check in report['checks'].items():
        detail = check.get('error') or check.get('reason') or ''
        if name == 'geometry':
            detail = f'{len(check["findings"])} findings'
            for item in check['findings']:
                lines.append(
                    f'  {item["kind"]} at {tuple(round(v, 1) for v in item["where"])}: '
                    f'{item["what"]}'
                )
        elif name == 'scenery' and 'findings' in check:
            detail = (
                f'{len(check["findings"])} spatial/attachment findings, '
                f'{len(check["placements"])} placement changes'
            )
            if check.get('attachment_checks') is not None:
                detail += f', {check["attachment_checks"]} attachment assemblies'
            for item in check['findings']:
                lines.append(
                    f'  {item["object"]}: {item["kind"]} at sector {item["sector"]}, '
                    f'piece {item["piece"]}' + (f' — {item["rule"]}' if item.get('rule') else '')
                )
        elif name == 'play' and 'finished_frame' in check:
            detail = (
                f'finish {check["finished_frame"]}, {len(check["checkpoints"])} checkpoints, '
                f'{len(check["falls_before_finish"])} falls, '
                f'{check["missed_rewards"]} rewards not reached'
            )
        elif name == 'connections' and 'count' in check:
            detail = f'{check["count"]} joints, {check["transitions"]} authored transitions'
        elif name == 'mechanisms' and 'findings' in check:
            detail = (
                f'{check["modeled"]} modeled, {len(check["unsupported"])} unsupported, '
                f'{len(check.get("unbounded_motion", []))} unbounded bodies, '
                f'{len(check["findings"])} conservative contacts, '
                f'{len(check["expected_interactions"])} declared interactions'
            )
            lines.extend(
                f'  {f["mechanism"]} [{f["phase"]}]: {f["rule"]} '
                f'against {f["contact"]["owner"]} ({f["contact"]["kind"]})'
                for f in check['findings']
            )
        elif name == 'mechanism_motion' and 'observed_instances' in check:
            detail = (
                f'{check["observed_instances"]} observed, '
                f'{len(check["envelope_escapes"])} envelope escapes, '
                f'{len(check["contacts"])} conservative pose contacts'
            )
        elif name == 'gameplay' and 'contracts' in check:
            detail = ', '.join(
                f'{sum(c["status"] == status for c in check["contracts"])} {status}'
                for status in ('passed', 'failed', 'skipped')
            )
            lines.extend(
                f'  {c["id"]}: {c["reason"]}' for c in check['contracts'] if c['status'] == 'failed'
            )
        elif name == 'resources' and 'count' in check:
            detail = (
                f'{check["count"]} descriptions, {check["instances"]} instances, '
                f'{len(check["findings"])} resource findings'
            )
            lines.extend(
                f'  {f["resource"]}: {f.get("reason", f.get("file", f["kind"]))}'
                for f in check['findings']
            )
        elif name == 'build':
            detail = f'{len(check["missing"])} missing, {len(check["changed"])} changed' + (
                f'; {detail}' if detail else ''
            )
            if check.get('native_assets') is False:
                detail += '; stand-in assets need native validation'
            for key in ('missing', 'changed', 'tool_changes', 'warnings'):
                lines.extend(f'  {key}: {value}' for value in check.get(key, []))
        lines.append(f'{name}: {check["status"]} — {detail}')
    lines.append(f'verification: {report["status"]}')
    return lines
