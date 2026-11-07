"""Fixed native key replays and ball observations for tolerance experiments."""

import csv
import json
import math
import selectors
import subprocess
import time

from .actions import ActionTrace
from .landings import observe
from .play import SENT

KEYS = ('up', 'down', 'left', 'right')
BALL_NAMES = ('Ball_Wood', 'Ball_Stone', 'Ball_Paper')


def read_inputs(path):
    """Keep the reference controller's actual keys, with absolute frame indices."""
    result = {}
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream):
            frame = int(row['frame'])
            if frame in result or any(row[key] not in ('0', '1') for key in KEYS):
                raise ValueError('reference inputs need unique frames and binary keys')
            result[frame] = sum(int(row[key]) << i for i, key in enumerate(KEYS))
    if not result:
        raise ValueError('reference input trace is empty')
    return result


def shift_inputs(inputs, first, frames, timing=0, phase=0):
    """Rehearse a native prefix for phase, then replay shifted keys after release.

    Timing shifts every transition relative to ball release, clipping early
    transitions at release. Phase replays the reference prefix in the SAME
    world before re-placing only the ball; it never sets a mechanism pose.
    """
    result = {}
    release = first + phase
    for frame in range(frames):
        if first <= frame < release - 10:
            result[frame] = inputs.get(frame, 0)
        elif frame >= release - 10:
            result[frame] = inputs.get(frame - phase - timing, 0)
        else:
            result[frame] = 0
    return result


def input_ranges(inputs):
    ranges = []
    for frame, mask in sorted(inputs.items()):
        if ranges and ranges[-1]['mask'] == mask and ranges[-1]['last'] + 1 == frame:
            ranges[-1]['last'] = frame
        else:
            ranges.append({'first': frame, 'last': frame, 'mask': mask})
    return ranges


def run_control(command, folder, game, inputs, snapshots):
    """Step fixed keys, acknowledging each native frame (including death pauses)."""
    requested = set(snapshots)
    observations = []
    deadline = time.monotonic() + 180
    with (folder / 'runtime.log').open('w') as log:
        process = subprocess.Popen(
            command,
            cwd=game,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=log,
            text=True,
            bufsize=1,
        )
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        try:
            expected = 0
            deaths = 0
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not selector.select(remaining):
                    raise TimeoutError('native key replay timed out')
                line = process.stdout.readline()
                if not line:
                    raise RuntimeError('native replay ended without its final snapshot')
                try:
                    state = json.loads(line)
                except ValueError:
                    log.write(line)
                    continue
                if not isinstance(state, dict) or 'frame' not in state or 'entities' not in state:
                    log.write(line)
                    continue
                frame = state['frame']
                if frame != expected:
                    raise RuntimeError(f'native replay frame mismatch: {frame} != {expected}')
                if frame in requested or state['gameplay']['deaths'] > deaths:
                    observations.append(state)
                deaths = state['gameplay']['deaths']
                if frame >= len(inputs):
                    process.stdin.write('quit\n')
                    process.stdin.flush()
                    break
                process.stdin.write(f'step 1 {inputs[frame]}\n')
                process.stdin.flush()
                expected += 1
            tail, _ = process.communicate(timeout=max(1, deadline - time.monotonic()))
            log.write(tail)
            if process.returncode:
                raise RuntimeError(f'native replay exited with {process.returncode}')
        finally:
            selector.close()
            if process.poll() is None:
                process.kill()
                process.communicate()
    (folder / 'observations.json').write_text(json.dumps(observations, indent=2) + '\n')


def ball_trace(folder):
    """Convert after-step physics poses to the next frame's pre-input ball trace."""
    with (
        (folder / 'physics.csv').open(newline='') as stream,
        (folder / 'steer.csv').open('w', newline='') as target,
    ):
        writer = csv.writer(target)
        writer.writerow(['frame', 'x', 'y', 'z', 'object_id', 'object_name'])
        previous = -1
        for row in csv.DictReader(stream):
            if row['name'] not in BALL_NAMES:
                continue
            frame = int(row['frame']) + 1
            if frame <= previous:
                raise ValueError('native physics trace contains ambiguous active balls')
            previous = frame
            writer.writerow([frame, *[row[f'm{k}'] for k in (12, 13, 14)], row['id'], row['name']])


def death_frames(folder, first):
    frames = set()
    for line in (folder / 'runtime.log').read_text(errors='replace').splitlines():
        match = SENT.match(line)
        if match and match[2] == 'Ball Off':
            frames.add(int(match[1]))
    observations = folder / 'observations.json'
    if observations.exists():
        for state in json.loads(observations.read_text()):
            if state['gameplay']['deaths']:
                frames.add(state['gameplay']['death_frame'])
    return sorted(frame for frame in frames if frame >= first)


def evaluate_landing(folder, case):
    all_rows = ActionTrace(folder, []).ball
    deaths = death_frames(folder, case['first_frame'])
    last = deaths[0] if deaths else case['frames']
    rows = [row for row in all_rows if case['first_frame'] <= row['frame'] < last]
    spawn_frame = case['first_frame'] - 9 if 'probe_options' in case else case['first_frame']
    initial = next((row for row in all_rows if row['frame'] >= spawn_frame), None)
    spawn = bool(
        initial
        and initial['name'] == 'Ball_' + case['ball']
        and math.dist(initial['position'], case['spawn_world']) < 3
    )
    result = observe(case['landing'], rows)
    return {
        'id': case['id'],
        'expected': 'complete',
        'spawn_observed': spawn,
        'evidence_sufficient': len(rows) >= 30,
        'landing': result,
        'death_frames': deaths,
        'first_frame': case['first_frame'],
        'last_frame': case['frames'] - 1,
        'status': 'passed' if spawn and result['status'] == 'passed' else 'failed',
    }
