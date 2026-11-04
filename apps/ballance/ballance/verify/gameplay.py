"""Evaluate gameplay contracts against observed identity and native events."""

import csv
import math
import re
from pathlib import Path

from .play import steer_from

SENT = re.compile(r'^\[(\d+)\]\s+.+?#(\d+) sends "([^"]+)" to All_Gameplay#(\d+)\s*$')


def inspect(folder: Path, specification: dict, sector=1, finish=None):
    contracts = specification['contracts']
    if not contracts:
        return {'status': 'skipped', 'reason': 'No authored gameplay contracts.'}
    events = []
    first = steer_from(sector)
    with (folder/'run.log').open(encoding='utf-8', errors='replace') as stream:
        for line in stream:
            match = SENT.match(line)
            if match:
                frame, sender, name, recipient = match.groups()
                frame = int(frame)
                if frame >= first and (finish is None or frame <= finish):
                    events.append({'frame': frame, 'event': name, 'sender': int(sender), 'recipient': int(recipient)})
    rows = []
    with (folder/'steer.csv').open(encoding='utf-8', newline='') as stream:
        reader = csv.DictReader(stream)
        if not {'frame', 'x', 'y', 'z', 'object_id', 'object_name'} <= set(reader.fieldnames or []):
            raise ValueError('gameplay contracts need actual object identity in steer.csv; rebuild nmo and rerun')
        previous = -1
        for row in reader:
            frame = int(row['frame'])
            position = [float(row[k]) for k in ('x', 'y', 'z')]
            if frame <= previous or not all(math.isfinite(v) for v in position) or int(row['object_id']) <= 0:
                raise ValueError('gameplay trace needs increasing frames, finite positions and valid object IDs')
            previous = frame
            if frame >= first and (finish is None or frame <= finish):
                rows.append({'frame': frame, 'position': position, 'object_id': int(row['object_id']), 'object_name': row['object_name']})
    if not rows:
        raise ValueError('gameplay contracts have no observed ball samples')
    checkpoints = [event['frame'] for event in events if event['event'] == 'Checkpoint reached']
    def in_sector(frame, target):
        # A checkpoint message still belongs to the sector it completes.
        return sector + sum(cp < frame for cp in checkpoints) == target
    outcomes = {}
    for contract in contracts:
        outcome = {'id': contract['id'], 'source': contract['source'], 'sector': contract['sector']}
        if contract['sector'] < sector:
            outcome.update(status='skipped', reason='Before requested starting sector; assumed by harness setup.')
        else:
            if contract['kind'] == 'passage':
                matches = [row for row in rows if in_sector(row['frame'], contract['sector']) and
                           math.dist(row['position'], contract['position']) <= contract['radius']]
            else:
                matches = [event for event in events if event['event'] == contract['event'] and in_sector(event['frame'], contract['sector'])]
            if not matches:
                outcome.update(status='failed', reason='Required passage or native event was not observed.')
            else:
                observed = matches[0]
                dependencies = [dep for dep in contract['after'] if outcomes[dep]['status'] != 'skipped' and
                                (outcomes[dep]['status'] != 'passed' or outcomes[dep]['frame'] > observed['frame'])]
                wrong = next((row for row in matches if contract['kind'] == 'passage' and row['object_name'] != 'Ball_'+contract['ball']), None)
                outcome.update(status='failed' if dependencies or wrong else 'passed', frame=observed['frame'], observed=observed)
                if dependencies:
                    outcome.update(reason='Passage/event preceded required conditions.', unmet=dependencies)
                if wrong:
                    outcome.update(reason='Wrong ball entered required passage.', expected_ball=contract['ball'], violation=wrong)
        outcomes[contract['id']] = outcome
    results = list(outcomes.values())
    return {'status': 'failed' if any(r['status']=='failed' for r in results) else 'passed',
            'contracts': results, 'events': events, 'scope': specification['scope'],
            'limits': 'Endpoint gates observe sampled passage, not all possible shortcuts or mechanism events. Earlier sectors in partial runs are setup assumptions.'}
