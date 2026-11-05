"""Sustained native actions, with stable body binding and actual ball identities.

Rotation is measured from the authored native rest orientation. A passage or
rotating fan decoration alone never satisfies a lift or hinge action.
"""

import csv
import math
import re


def rotation_degrees(matrix, reference):
    axes = []
    for pose in (matrix, reference):
        normalized = []
        for offset in (0, 4, 8):
            axis = pose[offset : offset + 3]
            length = math.sqrt(sum(v * v for v in axis))
            if length < 1e-8:
                raise ValueError('action pose has a degenerate basis')
            normalized.extend(v / length for v in axis)
        axes.append(normalized)
    cosine = (sum(a * b for a, b in zip(*axes)) - 1) / 2
    return math.degrees(math.acos(max(-1, min(1, cosine))))


def local_position(position, instance):
    x, y, z = [a - b for a, b in zip(position, instance['position'])]
    angle = math.radians(instance['yaw'])
    c, s = math.cos(angle), math.sin(angle)
    return [x * c - z * s, y, x * s + z * c]


def hinge_degrees(matrix, reference, axis):
    # Invalid transforms are missing evidence, never evidence of a blocked action.
    basis = []
    for offset in (0, 4, 8):
        vector = matrix[offset : offset + 3]
        length = math.sqrt(sum(v * v for v in vector))
        if length < 1e-8:
            raise ValueError('action pose has a degenerate basis')
        basis.append([v / length for v in vector])
    if any(
        abs(sum(a * b for a, b in zip(basis[i], basis[j]))) > 0.01
        for i, j in ((0, 1), (0, 2), (1, 2))
    ):
        raise ValueError('action pose has a nonorthogonal basis')
    offset = min((0, 4, 8), key=lambda i: abs(sum(reference[i + k] * axis[k] for k in range(3))))
    projected = []
    for pose in (reference, matrix):
        vector = pose[offset : offset + 3]
        along = sum(v * a for v, a in zip(vector, axis))
        projected.append([v - along * a for v, a in zip(vector, axis)])
    a, b = projected
    cross = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    return math.degrees(
        math.atan2(sum(v * n for v, n in zip(cross, axis)), sum(u * v for u, v in zip(a, b)))
    )


class ActionTrace:
    def __init__(self, folder, actions):
        self.ball = []
        with (folder / 'steer.csv').open(newline='') as stream:
            previous = -1
            for row in csv.DictReader(stream):
                frame = int(row['frame'])
                position = [float(row[k]) for k in ('x', 'y', 'z')]
                if (
                    frame <= previous
                    or not all(math.isfinite(v) for v in position)
                    or int(row['object_id']) <= 0
                    or row['object_name'] not in ('Ball_Wood', 'Ball_Stone', 'Ball_Paper')
                ):
                    raise ValueError('action ball trace needs ordered frames, finite poses and IDs')
                previous = frame
                self.ball.append(
                    {
                        'frame': frame,
                        'position': position,
                        'id': row['object_id'],
                        'name': row['object_name'],
                    }
                )
        self.bodies = {
            action['mechanism'] + '/' + action['body']: []
            for action in actions
            if action['type'] == 'rotation'
        }
        self.bindings = {}
        rotations = {
            action['mechanism'] + '/' + action['body']: action
            for action in actions
            if action['type'] == 'rotation'
        }
        if not rotations:
            return
        previous = -1
        with (folder / 'physics.csv').open(newline='') as stream:
            for row in csv.DictReader(stream):
                frame = int(row['frame'])
                if frame < previous:
                    raise ValueError('action body frames must be ordered')
                previous = frame
                matching = [
                    key
                    for key, action in rotations.items()
                    if re.fullmatch(re.escape(action['body']) + r'\d*', row['name'])
                ]
                if not matching:
                    continue
                matrix = [float(row[f'm{k}']) for k in range(16)]
                if not all(math.isfinite(v) for v in matrix):
                    raise ValueError('action body pose must be finite')
                identifier = row['id']
                if identifier not in self.bindings:
                    candidates = [
                        key
                        for key in matching
                        if math.dist(matrix[12:15], rotations[key]['reference'][12:15]) < 1
                    ]
                    if len(candidates) > 1:
                        raise ValueError('ambiguous action body identity')
                    if not candidates:
                        continue
                    self.bindings[identifier] = candidates[0]
                key = self.bindings[identifier]
                self.bodies[key].append(
                    {'frame': frame, 'matrix': matrix, 'id': identifier, 'name': row['name']}
                )

    def samples(self, action):
        if action['type'] == 'rotation':
            return self.bodies[action['mechanism'] + '/' + action['body']]
        return self.ball

    def measure(self, action, row):
        if action['type'] == 'rotation':
            degrees = hinge_degrees(row['matrix'], action['reference'], action['axis'])
            magnitude = (
                abs(degrees)
                if action['direction'] == 'either'
                else degrees
                if action['direction'] == 'positive'
                else -degrees
            )
            return magnitude >= action['min_degrees'], {'degrees': degrees}
        position = local_position(row['position'], action['instance'])
        box = action['box']
        inside = all(box[k] <= v <= box[k + 1] for k, v in zip((0, 2, 4), position))
        return inside, {'local_position': position, 'ball': row['name']}

    def inspect(self, action, first, last, *, eligible=lambda frame: True):
        streak = 0
        previous = None
        first_match = None
        observations = []
        samples = 0
        continuous = True
        observed_first = None
        origin = None
        previous_ball = None
        for row in self.samples(action):
            frame = row['frame']
            if not first <= frame <= last or not eligible(frame):
                continue
            samples += 1
            observed_first = frame if observed_first is None else observed_first
            if previous is not None and frame != previous + 1:
                continuous = False
            matched, measurement = self.measure(action, row)
            if 'rise' in action:
                rise = action['rise']
                if origin is not None and (
                    frame - origin['frame'] > rise['within']
                    or row['id'] != origin['id']
                    or row['name'] != origin['name']
                    or previous_ball is None
                    or frame != previous_ball['frame'] + 1
                    or math.dist(row['position'], previous_ball['position']) > 20
                ):
                    origin = None
                local = measurement['local_position']
                if origin is None and all(
                    rise['from'][k] <= v <= rise['from'][k + 1] for k, v in zip((0, 2, 4), local)
                ):
                    origin = row
                gain = row['position'][1] - origin['position'][1] if origin else None
                rising = (
                    previous_ball is not None and row['position'][1] > previous_ball['position'][1]
                )
                matched = matched and origin is not None and gain >= rise['min_height'] and rising
                measurement.update(rise=gain, from_frame=origin['frame'] if origin else None)
                previous_ball = row
            consecutive = previous is not None and frame == previous + 1
            streak = (streak + 1 if consecutive else 1) if matched else 0
            previous = frame
            if streak == action['hold']:
                observation = {
                    'frame': frame,
                    'since': frame - streak + 1,
                    'runtime_id': row['id'],
                    **measurement,
                }
                observations.append(observation)
                first_match = first_match or observation
        return {
            'status': 'passed' if first_match else 'failed',
            'observed': first_match,
            'samples': samples,
            'complete_window': continuous and observed_first == first and previous == last,
            'occurrences': observations,
            'mechanism': action['mechanism'],
            'reason': None if first_match else 'Required sustained action was not observed.',
        }
