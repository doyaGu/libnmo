"""A composition of our own that plays a level in place of the game's
base.cmo: no intro, no menu, straight into level 1.

The level's gameplay comes from the game's own files in 3D Entities
(Gameplay, Levelinit, Balls, the PH modules), loaded as base.cmo loads them.
Those files ask base.cmo for little, all by name: the "Level" object their
messages go to, the arrays GameSettings, DB_Options, Loading_Parameter and
RegisterNMO, and the script Loading_Manager, which Levelinit runs to load the
level's modules. The harness has those, and two scripts on "Level":

- "Test Start" loads the main files (with Loading_Manager), builds the
  level (Levelinit_build) and starts it as base.cmo's "Start Level" does;
- "Loading_Manager" loads the rows of the array Loading_Parameter names, from
  its Start row to its End row (-1, the last), and starts each file's
  <name>_Init script when the row asks.

GameSettings has Godmode on (the game's debug keys: 3 finishes the level, 2
gives a life) and the tutorial off.  The harness is written with
`nmo patch apply`: a project manifest for the objects, `nmo data fill` for
the arrays, and an edit plan for the scripts.
"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
from pathlib import Path

from ..core.nmo import Nmo

DATA_DIR = Path(__file__).resolve().parents[4] / 'data'  # libnmo's BB and type tables

STRING, INTEGER, BOOLEAN = '6BD010E2-115617EA', '5A5716FD-44E276D7', '1AD52A8E-5E741920'
SCRIPT, BEOBJECT, ARRAY = '7EA4176D-1B405D30', '71D80779-402F42F3', '024D52F1-678223B2'
GROUP = '5C0F151B-6F0547FD'
COLOR, VECTOR2D = '57D42FEE-7CBB3B91', '4EFCB34A-6079E42F'

OBJECT_LOAD, EXECUTE_SCRIPT = '7BD977D7-26396C0C', '706C5A40-5BB31A0B'
ACTIVATE_SCRIPT, SEND_MESSAGE = '4C7E7BC3-0B693155', 'A20E8D5B-DF002150'
GET_ROW, ITERATOR = '33B77F41-07B95C45', '198F0AF9-0268249F'
BINARY_SWITCH, GET_SUBSTRING = 'EB506901-984AFCCC', '10B16051-26173B39'
DELAYER, SET_ROW = '15D472A5-3BEA409F', '62E87901-2DF007DD'
SET_ACTIVE_CAMERA = '368F0AB1-2D8957E4'

ADDITION, SUBTRACTION = '33CC6B49-3589282B', '67641171-6499077A'
GET_LENGTH, GET_OBJECT_BY_NAME = '556A69AF-076E3F09', '599203F6-23B06096'

FIRST_CHECKPOINT_MS = 4000  # from the level's start to the first checkpoint reached
# (the ball live, the camera flown in)
SECTOR_SETTLE_MS = 1500  # a checkpoint reached: the sectors swapped

# AllLevel's columns (Levelinit.nmo's array), as Get Row and Set Row take them
ALL_LEVEL = [
    ('Levelfile', STRING),
    ('StartBall', STRING),
    ('StartResetpoint', INTEGER),
    ('Sky', STRING),
    ('Light', COLOR),
    ('Skytranslation', VECTOR2D),
    ('LevelBonus', INTEGER),
    ('Music', INTEGER),
]

# The main files, as base.cmo's MainLoading has them less the menu level and
# intro; True where the file has a <name>_Init script to start.  Menu.nmo's
# scripts stay stopped: Gameplay takes its Keyboard array and its Font_1 (the
# points' digits) from it
MAIN_FILES = [
    ('Language.nmo', False),
    ('Sound.nmo', True),
    ('Menu.nmo', False),
    ('Gameplay.nmo', True),
    ('AnimTrafo.nmo', True),
    ('Camera.nmo', False),
    ('Levelinit.nmo', True),
    ('Balls.nmo', True),
]

# The arrays, laid out as base.cmo's: the game's files read their columns by index
ARRAYS = {
    'GameSettings': {
        'columns': [
            {'name': 'Display Driver', 'type': 'int'},
            {'name': 'ScreenMode', 'type': 'int'},
            {'name': 'Registry Path', 'type': 'string'},
            {'name': 'relative File Path', 'type': 'string'},
            {'name': 'Database Name', 'type': 'string'},
            {'name': 'Player Active?', 'type': 'parameter', 'parameter': BOOLEAN},
            {'name': 'Godmode?', 'type': 'parameter', 'parameter': BOOLEAN},
            {'name': 'Debug?', 'type': 'parameter', 'parameter': BOOLEAN},
            {'name': 'Fullversion?', 'type': 'int'},
            {'name': 'Language', 'type': 'int'},
            {'name': 'Volume', 'type': 'float'},
            {'name': 'Debug Font', 'type': 'int'},
            {'name': 'Tutorial?', 'type': 'parameter', 'parameter': BOOLEAN},
            {'name': 'LowMemory', 'type': 'parameter', 'parameter': BOOLEAN},
            {'name': 'Evaluation', 'type': 'parameter', 'parameter': BOOLEAN},
        ],
        'rows': [
            [
                0,
                0,
                'Software\\Ballance\\',
                '',
                'Database.tdb',
                False,
                True,
                False,
                0,
                1,
                0.0,
                0,
                False,
                False,
                False,
            ]
        ],
    },
    'DB_Options': {
        'columns': [
            {'name': 'Volume', 'type': 'float'},
            {'name': 'Synch to Screen?', 'type': 'int'},
            {'name': 'Key Forward', 'type': 'int'},
            {'name': 'Key Backward', 'type': 'int'},
            {'name': 'Key Left', 'type': 'int'},
            {'name': 'Key Right', 'type': 'int'},
            {'name': 'Key Rotate Cam', 'type': 'int'},
            {'name': 'Key Lift Cam', 'type': 'int'},
            {'name': 'Invert Cam Rotation?', 'type': 'int'},
            {'name': 'LastPlayer', 'type': 'string'},
            {'name': 'CloudLayer?', 'type': 'int'},
        ],
        'rows': [[1.0, 0, 68, 69, 70, 71, 39, 53, 0, 'name', 1]],
    },
    'Version': {'columns': [{'name': 'Version String', 'type': 'string'}], 'rows': [['v1.1']]},
    'MainLoading': {
        'columns': [
            {'name': 'rel.Path', 'type': 'string'},
            {'name': 'NMO-Name', 'type': 'string'},
            {'name': 'Start init_Script?', 'type': 'parameter', 'parameter': BOOLEAN},
        ],
        'rows': [['3D Entities\\', name, init] for name, init in MAIN_FILES],
    },
    'Loading_Parameter': {
        'columns': [
            {'name': 'ParameterArray', 'type': 'parameter', 'parameter': ARRAY},
            {'name': 'Start', 'type': 'int'},
            {'name': 'End', 'type': 'int'},
            {'name': 'Loading ID', 'type': 'int'},
        ],
        'rows': [['', 0, -1, 0]],  # MainLoading, set once the objects have ids
    },
    'RegisterNMO': {
        'columns': [
            {'name': 'All_Group', 'type': 'parameter', 'parameter': GROUP},
            {'name': 'Load ID', 'type': 'int'},
        ],
        'rows': [],
    },
}


class Plan:
    """An edit plan's operations, each later one free to name what an
    earlier one made by (operation, handle)."""

    def __init__(self) -> None:
        self.ops: list[dict] = []

    def add(self, op: dict) -> int:
        self.ops.append(op)
        return len(self.ops) - 1

    @staticmethod
    def ref(key: str, where: tuple[int, str] | int) -> dict:
        if isinstance(where, int):
            return {f'{key}_id': where}
        return {f'{key}_operation': where[0] + 1, f'{key}_handle': where[1]}  # counted from 1

    def node(self, parent: int, guid: str, name: str, values: dict | None = None) -> int:
        op = {'op': 'add_node', 'behavior_id': parent, 'guid': guid, 'name': name}
        if guid == SEND_MESSAGE:  # its Message pin's default needs a message entry
            op['manager_entry'] = {'policy': 'create_missing', 'schema': 'message'}
        n = self.add(op)
        for pin, value in (values or {}).items():
            self.value((n, f'input_param:{pin}'), value)
        return n

    def value(self, parameter: tuple[int, str] | int, value) -> None:
        op = {'op': 'set_parameter_value', **self.ref('parameter', parameter)}
        if isinstance(value, bool):
            op['value'] = 'TRUE' if value else 'FALSE'
        elif isinstance(value, Message):
            op['value'] = value.text
            op['manager_entry'] = {'policy': 'create_missing', 'schema': 'message'}
        else:
            op['value'] = str(value)
        self.add(op)

    def link(
        self, parent: int, source: tuple[int, str] | int, target: tuple[int, str], delay: int = 0
    ) -> None:
        op = {
            'op': 'add_behavior_link',
            'parent_id': parent,
            **self.ref('from', source),
            **self.ref('to', target),
        }
        if delay:
            op['activation_delay'] = delay
        self.add(op)

    def wire(self, source: tuple[int, str], target: tuple[int, str]) -> None:
        """target (an input parameter) reads source."""
        self.add(
            {'op': 'connect_parameter', **self.ref('source', source), **self.ref('target', target)}
        )

    def local(self, owner: int, type_guid: str, name: str, value=None) -> tuple[int, str]:
        p = self.add(
            {
                'op': 'add_parameter',
                'owner_id': owner,
                'kind': 'local',
                'type_guid': type_guid,
                'name': name,
            }
        )
        if value is not None:
            self.value((p, 'parameter'), value)
        return (p, 'parameter')

    def output(
        self, node: tuple[int, str], type_guid: str, name: str, kind: str = 'out'
    ) -> tuple[int, str]:
        """An output (or, kind "in", input) parameter added to node."""
        return (
            self.add(
                {
                    'op': 'add_parameter',
                    **self.ref('owner', node),
                    'kind': kind,
                    'type_guid': type_guid,
                    'name': name,
                }
            ),
            'parameter',
        )

    def operation(
        self,
        parent: int,
        guid: str,
        result_type: str,
        name: str,
        in1: tuple[int, str],
        in2: tuple[int, str] | None = None,
    ) -> tuple[int, str]:
        """An operation of parent's graph on in1 (and in2), giving result_type;
        its output, for pins to read."""
        result = self.local(parent, result_type, name)
        op = {
            'op': 'add_operation',
            'parent_id': parent,
            'operation_guid': guid,
            **self.ref('in1', in1),
            **self.ref('out', result),
        }
        if in2 is not None:
            op.update(self.ref('in2', in2))
        return (self.add(op), 'out')

    def by_name(self, parent: int, name: str, type_guid: str) -> tuple[int, str]:
        """The object called name, of type_guid, found when it is read."""
        text = self.local(parent, STRING, f'{name} Name', name)
        return self.operation(parent, GET_OBJECT_BY_NAME, type_guid, name, text)


class Message(str):
    """A message name, as a pin of the Message type takes it."""

    @property
    def text(self) -> str:
        return str(self)


def send(plan: Plan, script: int, level: int, message: str, to: str, name: str) -> int:
    """A Send Message of message from level to the object called to."""
    node = plan.node(script, SEND_MESSAGE, name, {'Message': Message(message)})
    plan.value((node, 'target'), f'object:{level}')  # the sender
    plan.wire(plan.by_name(script, to, BEOBJECT), (node, 'input_param:Dest'))
    return node


def start_script(
    plan: Plan, script: int, level: int, camera: int, loading_manager: int, sector: int, ball: str
) -> None:
    """Test Start: the main files, the level built and started.

    Past sector 1, the level starts where sector does, as the game starts a
    level: AllLevel's start ball (ball: Wood, Stone or Paper) and start
    reset point (sector's) set before the level is built.  The checkpoints
    before it are then reached as the game reaches them, all but the last
    by the harness, 1.5 s apart; the last the ball reaches itself, standing
    on it.  (The game has two checkpoint objects, moved on to the next
    checkpoint in turn, so none is left waiting where the ball never goes.)"""
    start = (
        plan.add({'op': 'add_io', 'behavior_id': script, 'kind': 'input', 'name': 'Start'}),
        'io',
    )
    load = plan.node(
        script,
        EXECUTE_SCRIPT,
        'Load Main Files',
        {'Reset ?': True, 'Script': f'object:{loading_manager}'},
    )
    build = plan.node(script, EXECUTE_SCRIPT, 'Build Level', {'Reset ?': True})
    plan.wire(plan.by_name(script, 'Levelinit_build', SCRIPT), (build, 'input_param:Script'))
    ingame = plan.node(script, ACTIVATE_SCRIPT, 'Start Gameplay', {'Reset ?': True})
    plan.wire(plan.by_name(script, 'Gameplay_Ingame', SCRIPT), (ingame, 'input_param:Script'))
    sound = send(plan, script, level, 'Start Level', 'All_Sound', 'Start Level Sound')

    # the view while the level is built, before Gameplay's camera takes over:
    # black (base.cmo fades it out)
    view = plan.node(script, SET_ACTIVE_CAMERA, 'Black View')
    plan.value((view, 'target'), f'object:{camera}')
    plan.link(script, start, (view, 'input:In'))
    plan.link(script, (view, 'output:Out'), (load, 'input:In'))
    last = (load, 'output:Out')
    if sector > 1 or ball != 'Wood':
        levels = plan.by_name(script, 'AllLevel', ARRAY)
        row = plan.node(script, GET_ROW, 'Get Level 1', {'Row Index': 0})
        plan.wire(levels, (row, 'target'))
        cells = [plan.output((row, 'node'), type_guid, name) for name, type_guid in ALL_LEVEL]
        start_at = plan.node(script, SET_ROW, f'Start At Sector {sector}', {'Row Index': 0})
        plan.wire(levels, (start_at, 'target'))
        given = {
            'StartBall': plan.local(script, STRING, 'Start Ball', f'Ball_{ball}'),
            'StartResetpoint': plan.local(script, INTEGER, 'Start Resetpoint', sector - 1),
        }
        for (name, type_guid), cell in zip(ALL_LEVEL, cells):
            plan.wire(given.get(name, cell), plan.output((start_at, 'node'), type_guid, name, 'in'))
        plan.link(script, last, (row, 'input:In'))
        plan.link(script, (row, 'output:Found'), (start_at, 'input:In'))
        last = (start_at, 'output:Out')
    plan.link(script, last, (build, 'input:In'))
    plan.link(script, (build, 'output:Out'), (ingame, 'input:In'))
    plan.link(script, (ingame, 'output:Out'), (sound, 'input:In'))

    last = (sound, 'output:Out')
    if sector <= 2:
        return  # at sector 2 the ball stands on checkpoint 1, which it reaches at once

    def then(node: int, out: str = 'Out') -> None:
        nonlocal last
        plan.link(script, last, (node, 'input:In'))
        last = (node, f'output:{out}')

    then(plan.node(script, DELAYER, 'Sectors Up', {'Time to Wait': FIRST_CHECKPOINT_MS}))
    for n in range(1, sector - 1):
        then(
            send(
                plan, script, level, 'Checkpoint reached', 'All_Gameplay', f'Checkpoint {n} Reached'
            )
        )
        then(
            plan.node(
                script, DELAYER, f'Sector {n + 1} Activated', {'Time to Wait': SECTOR_SETTLE_MS}
            )
        )


def loading_manager_script(plan: Plan, script: int, loading_parameter: int) -> None:
    """Loading_Manager: the rows Loading_Parameter names loaded, and their
    <name>_Init scripts started where a row asks."""
    start = (
        plan.add({'op': 'add_io', 'behavior_id': script, 'kind': 'input', 'name': 'Start'}),
        'io',
    )
    row = plan.node(script, GET_ROW, 'Get Loading Parameter', {'Row Index': 0})
    plan.value((row, 'target'), f'object:{loading_parameter}')
    # Get Row gives a row's cells on outputs of their types, one a column
    outputs = {}
    for name, type_guid in (
        ('ParameterArray', ARRAY),
        ('Start', INTEGER),
        ('End', INTEGER),
        ('Loading ID', INTEGER),
    ):
        outputs[name] = plan.output((row, 'node'), type_guid, name)

    files = plan.node(script, ITERATOR, 'Each File', {'Backward': False})
    plan.wire(outputs['ParameterArray'], (files, 'target'))
    plan.wire(outputs['Start'], (files, 'input_param:Starting Row'))
    plan.wire(outputs['End'], (files, 'input_param:Ending Row'))
    columns = {}
    for name, type_guid in (
        ('rel.Path', STRING),
        ('NMO-Name', STRING),
        ('Start init_Script?', BOOLEAN),
    ):
        columns[name] = plan.output((files, 'node'), type_guid, name)

    load = plan.node(
        script,
        OBJECT_LOAD,
        'Load File',
        {'Add To Current Scene': True, 'Reuse Meshes': True, 'Reuse Materials/Textures': True},
    )
    path = plan.operation(
        script, ADDITION, STRING, 'File Path', columns['rel.Path'], columns['NMO-Name']
    )
    plan.wire(path, (load, 'input_param:File'))

    init = plan.node(script, BINARY_SWITCH, 'Has Init Script?')
    plan.wire(columns['Start init_Script?'], (init, 'input_param:Condition'))

    # <name>_Init: the file's name less ".nmo"
    stem = plan.node(script, GET_SUBSTRING, 'File Stem', {'Start': 0})
    plan.wire(columns['NMO-Name'], (stem, 'input_param:String'))
    length = plan.operation(script, GET_LENGTH, INTEGER, 'Name Length', columns['NMO-Name'])
    four = plan.local(script, INTEGER, 'Extension Length', 4)
    plan.wire(
        plan.operation(script, SUBTRACTION, INTEGER, 'Stem Length', length, four),
        (stem, 'input_param:Length'),
    )
    suffix = plan.local(script, STRING, 'Init Suffix', '_Init')
    init_name = plan.operation(
        script, ADDITION, STRING, 'Init Name', (stem, 'output_param:SubString'), suffix
    )
    activate = plan.node(script, ACTIVATE_SCRIPT, 'Start Init Script', {'Reset ?': True})
    plan.wire(
        plan.operation(script, GET_OBJECT_BY_NAME, SCRIPT, 'Init Script', init_name),
        (activate, 'input_param:Script'),
    )

    plan.link(script, start, (row, 'input:In'))
    plan.link(script, (row, 'output:Found'), (files, 'input:In'))
    plan.link(script, (files, 'output:Loop Out'), (load, 'input:Load'))
    plan.link(script, (load, 'output:Loaded'), (init, 'input:In'))
    plan.link(script, (load, 'output:Failed'), (files, 'input:Loop In'))
    plan.link(script, (init, 'output:True'), (stem, 'input:In'))
    plan.link(script, (init, 'output:False'), (files, 'input:Loop In'))
    plan.link(script, (stem, 'output:Out'), (activate, 'input:In'))
    plan.link(script, (activate, 'output:Out'), (files, 'input:Loop In'))


def write(
    out: Path,
    nmo: Nmo,
    sector: int = 1,
    ball: str = 'Wood',
    probe_position=None,
    probe_physics=None,
) -> None:
    """Write native gameplay, optionally with an isolated local contact probe.

    A probe unphysicalizes the player before moving it, then recreates its
    two-unit sphere with the native ball-table material parameters. It never
    changes the level or mechanism bodies and is not used for full play tests.
    """
    if probe_position is not None and probe_physics is None:
        raise ValueError('a local probe requires native ball physics parameters')
    env = dict(os.environ)
    env.setdefault('NMO_DATA_DIR', str(DATA_DIR))

    def run(*args: str) -> str:
        result = subprocess.run([nmo.executable, *args], capture_output=True, text=True, env=env)
        if result.returncode != 0:
            raise RuntimeError(
                f'nmo {" ".join(args[:2])} failed: {(result.stderr or result.stdout).strip()}'
            )
        return result.stdout

    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        manifest = {
            'version': 1,
            'document': {'name': 'Test Base'},
            'scenes': [
                {
                    'name': 'Test Base',
                    'environment': {'background_color': [0, 0, 0, 1]},
                    'objects': [
                        # far above everything: it sees nothing
                        {
                            'name': 'Test Camera',
                            'class': 'CKCamera',
                            'transform': {'position': [0, 1000000, 0]},
                        },
                        {
                            'name': 'Level',
                            'class': 'CKBeObject',
                            'scripts': [
                                {'name': 'Test Start'},
                                {'name': 'Loading_Manager'},
                                *([{'name': 'Probe Spawn'}] if probe_position is not None else []),
                            ],
                        },
                        *({'name': name, 'class': 'CKDataArray'} for name in ARRAYS),
                    ],
                }
            ],
        }
        (work / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
        current = work / 'objects.cmo'
        run('patch', 'apply', '--project', str(work / 'manifest.json'), '-o', str(current))

        for name, table in ARRAYS.items():
            if name == 'Loading_Parameter':
                continue  # needs MainLoading's id
            (work / 'table.json').write_text(json.dumps(table), encoding='utf-8')
            filled = work / f'{name}.cmo'
            run(
                'data',
                'fill',
                '--name',
                name,
                '--table',
                str(work / 'table.json'),
                str(current),
                '-o',
                str(filled),
            )
            current = filled
        ids = {
            o['name']: o['id']
            for o in json.loads(run('-f', 'json', 'object', 'list', str(current)))['data'][
                'objects'
            ]
        }
        table = dict(ARRAYS['Loading_Parameter'])
        table['rows'] = [[f'object:{ids["MainLoading"]}', 0, -1, 0]]
        (work / 'table.json').write_text(json.dumps(table), encoding='utf-8')
        filled = work / 'Loading_Parameter.cmo'
        run(
            'data',
            'fill',
            '--name',
            'Loading_Parameter',
            '--table',
            str(work / 'table.json'),
            str(current),
            '-o',
            str(filled),
        )
        current = filled

        plan = Plan()
        start_script(
            plan,
            ids['Test Start'],
            ids['Level'],
            ids['Test Camera'],
            ids['Loading_Manager'],
            sector,
            ball,
        )
        loading_manager_script(plan, ids['Loading_Manager'], ids['Loading_Parameter'])
        if probe_position is not None:
            script = ids['Probe Spawn']
            entry = (
                plan.add({'op': 'add_io', 'behavior_id': script, 'kind': 'input', 'name': 'In'}),
                'io',
            )
            delay = plan.node(
                script, DELAYER, 'Wait For Native Sector', {'Time to Wait': 6000 + setup_ms(sector)}
            )
            position = plan.node(
                script,
                'E456E78A-456789AA',
                'Place Probe Ball',
                {'Position': ','.join(map(str, probe_position))},
            )
            name = plan.local(script, STRING, 'Probe Ball Name', f'Ball_{ball}')
            entity = plan.operation(
                script, '428009B4-1CAA5C78', '5B8A05D5-31EA28D4', 'Probe Ball', name
            )
            plan.wire(entity, (position, 'target'))
            plan.link(script, entry, (delay, 'input:In'))
            # CK disables collisions for a sphere whose surface name is empty.
            physics = plan.node(
                script,
                '7522370E-37EC15EC',
                'Probe Ball Physics',
                dict(probe_physics, **{'Collision Surface': f'Ball_{ball}'}),
            )
            plan.wire(entity, (physics, 'target'))
            plan.value((physics, 'local_param:Convex Count'), 0)
            plan.value((physics, 'local_param:Ball Count'), 1)
            plan.link(script, (delay, 'output:Out'), (physics, 'input:Unphysicalize'))
            plan.link(script, (physics, 'output:Out2'), (position, 'input:In'))
            plan.link(script, (position, 'output:Out'), (physics, 'input:Physicalize'))
        scripted = work / 'scripted.cmo'
        (work / 'plan.json').write_text(
            json.dumps(
                {
                    'version': 2,
                    'input': str(current),
                    'output': str(scripted),
                    'operations': plan.ops,
                },
                indent=1,
            ),
            encoding='utf-8',
        )
        run('patch', 'apply', str(work / 'plan.json'))
        if probe_position is not None:
            block = nmo.data('behavior', 'find', '--name', 'Probe Ball Physics', str(scripted))[
                'results'
            ][0]
            detail = nmo.data('behavior', 'show', str(block['id']), str(scripted))
            convex = next(p['id'] for p in detail['input_parameters'] if p['name'] == 'Convex')
            shape = Plan()
            shape.add({'op': 'remove_parameter', 'parameter_id': convex, 'detach': True})
            centre = shape.output(block['id'], '48824EAE-2FE47960', 'ball position 1', 'in')
            shape.value(centre, '0,0,0')
            radius = shape.output(block['id'], '47884C3F-432C2C20', 'ball radius 1', 'in')
            shape.value(radius, 2)
            (work / 'shape.json').write_text(
                json.dumps(
                    {
                        'version': 2,
                        'input': str(scripted),
                        'output': str(work / 'shaped.cmo'),
                        'operations': shape.ops,
                    }
                )
            )
            run('patch', 'apply', str(work / 'shape.json'))
            scripted = work / 'shaped.cmo'
        # Loading_Manager runs when called, not at the start
        manager = str(ids['Loading_Manager'])
        run(
            'object',
            'set-field',
            '--id',
            manager,
            'has_single_activity',
            'true',
            str(scripted),
            '-o',
            str(work / 'called.cmo'),
        )
        run(
            'object',
            'set-field',
            '--id',
            manager,
            'single_activity_flags',
            '0',
            str(work / 'called.cmo'),
            '-o',
            str(out),
        )


CACHE = Path(tempfile.gettempdir()) / 'ballance-harness'


def setup_ms(sector: int) -> int:
    """How long, from the level's start, the harness takes to start it at
    sector; the ball must stand still till then."""
    if sector <= 2:
        return 0
    return FIRST_CHECKPOINT_MS + SECTOR_SETTLE_MS * (sector - 2)


def ensure(nmo: Nmo, sector: int = 1, ball: str = 'Wood') -> Path:
    """The harness starting at sector with ball, written to CACHE the first
    time and when this module has changed since."""
    path = CACHE / (
        'test_base.cmo' if sector == 1 and ball == 'Wood' else f'test_base_{sector}_{ball}.cmo'
    )
    if not path.is_file() or path.stat().st_mtime < Path(__file__).stat().st_mtime:
        path.parent.mkdir(parents=True, exist_ok=True)
        write(path, nmo, sector, ball)
    return path
