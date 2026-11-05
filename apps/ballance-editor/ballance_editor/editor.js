/* Direct manipulation and document operations. JSON is an optional source view. */
let handles = [];
let handleDrag = null;
let livePiece = null;
let tool = 'select';
let vertical = false;
let placing = null;
let appendTarget = null;
let viewDrag = null;
let sketch = null;
const words = {
    offset: '相对原始安装点的偏移',
    yaw: '相对原始朝向（度）',
    sector: '激活小节',
    road: '长度',
    rail: '长度',
    beam: '长度',
    drop: '下降高度',
    hump: '拱高',
    width: '宽度',
    length: '长度',
    gauge: '轨距',
    radius: '半径',
    bank: '倾角',
    depth: '厚度 / 深度',
    border: '砖边宽度',
    step: '采样步长',
    tolerance: '采样精度',
    spacing: '间距',
    count: '数量',
    side: '方向',
    mirror: '镜像',
    repeat: '重复次数',
    rise: '抬升高度',
    fan: '抬升高度',
    sack: '悬挂摆锤',
    point: '额外分数',
    railings: '护栏',
    railing: '外侧护栏',
    hairpin: '折返弯',
    gap: '间隙',
    long: '加长',
    keel: '底部纵梁',
    resource: '资源',
    with: '模板参数',
    parameters: '默认参数',
    rail_path: '双轨曲线',
    beam_path: '单轨曲线',
    floor_path: '自由地面',
    floor_polygon: '平台轮廓',
    connector: '连接器',
    segments: '曲线段',
    bezier: '贝塞尔控制点',
    line: '终点',
    arc: '圆弧',
    wave: '波浪',
    weave: '交织线',
    eased_turn: '缓和弯',
    outline: '外轮廓',
    holes: '洞口',
    entry: '入口',
    exit: '出口',
    edge: '边序号',
    ports: '分支端口',
    ribs: '支架',
    at: '局部位置',
    up: '升高',
    elements: '附属元素',
    max_shift: '最大避让距离',
    mode: '依附模式',
    allow_brace: '允许支撑梁',
    allow_omit: '允许省略',
    parent: '所属对象',
    clearance: '死亡区净空',
    tile: '分区尺寸',
    quantum: '高度步进',
    margin: '外围范围',
    keepouts: '恢复空间',
    turn: '转向',
    rail_turn: '转向',
    beam_turn: '转向',
};
const options = {
    width: ['narrow', 'wide'],
    turn: ['left', 'right'],
    rail_turn: ['left', 'right'],
    beam_turn: ['left', 'right'],
    side: ['left', 'right'],
    exit: ['straight', 'left', 'right'],
    mode: ['locked', 'nearby', 'attached'],
    trafo: ['wood', 'stone', 'paper'],
};
function atPath(value, path) {
    return path.reduce((v, k) => v[k], value);
}
function putPath(value, path, next) {
    let v = value;
    for (const k of path.slice(0, -1)) {
        v = v[k];
    }
    v[path.at(-1)] = next;
}
function pendingPiece() {
    return JSON.parse($('piece').value);
}
function propertyValue(path, value) {
    const piece = pendingPiece();
    putPath(piece, path, value);
    $('piece').value = JSON.stringify(piece, null, 2);
    livePiece = piece;
    draw();
    documentAction('set');
}
function renderProperties() {
    $('structureHint').textContent = currentEntity()?.source?.instances?.length
        ? '结构操作作用于整次模板调用（包含重复实例）。'
        : '新元素接在所选元素之后，后续路线随连接移动。';
    const root = $('properties');
    root.replaceChildren();
    let value;
    try {
        value = pendingPiece();
    } catch {
        return;
    }
    const build = (v, path, container) => {
        const key = path.at(-1);
        const label = words[key] || String(key);
        if (path.length === 1 && ['id', 'name', 'branches', 'use'].includes(key)) {
            return;
        }
        if (
            Array.isArray(v) &&
            v.length === 3 &&
            v.every((x) => typeof x === 'number') &&
            ['at', 'line', 'bezier'].some((k) => path.includes(k))
        ) {
            const group = document.createElement('fieldset');
            const legend = document.createElement('legend');
            legend.textContent = typeof key === 'number' ? `控制点 ${key + 1}` : label;
            group.append(legend);
            const row = document.createElement('div');
            row.className = 'vector';
            v.forEach((n, i) => {
                const lab = document.createElement('label');
                lab.textContent = ['前进', '右移', '升高'][i];
                const input = document.createElement('input');
                input.type = 'number';
                input.step = '.1';
                input.value = n;
                input.setAttribute('aria-label', `${path.join('.')} ${lab.textContent}`);
                input.onchange = () => {
                    if (input.value !== '' && Number.isFinite(input.valueAsNumber)) {
                        propertyValue([...path, i], input.valueAsNumber);
                    }
                };
                lab.append(input);
                row.append(lab);
            });
            group.append(row);
            container.append(group);
            return;
        }
        if (v && typeof v === 'object') {
            const group = document.createElement(path.length ? 'fieldset' : 'div');
            if (path.length) {
                const legend = document.createElement('legend');
                legend.textContent = typeof key === 'number' ? `第 ${key + 1} 项` : label;
                group.append(legend);
            }
            for (const [k, x] of Object.entries(v)) {
                build(x, [...path, Array.isArray(v) ? Number(k) : k], group);
            }
            container.append(group);
            return;
        }
        if (v === null) {
            return;
        }
        const row = document.createElement('label');
        row.className = 'property';
        const text = document.createElement('span');
        text.textContent = label;
        row.append(text);
        let input;
        if (typeof v === 'boolean') {
            input = document.createElement('input');
            input.type = 'checkbox';
            input.checked = v;
            input.onchange = () => propertyValue(path, input.checked);
        } else if (typeof v === 'string' && (options[key]?.includes(v) || key === 'resource')) {
            input = document.createElement('select');
            for (const name of key === 'resource'
                ? (state.palette || [])
                      .filter((p) => p.key.startsWith('resource:'))
                      .map((p) => p.key.slice(9))
                : options[key]) {
                input.add(
                    new Option(
                        {
                            left: '左',
                            right: '右',
                            straight: '直行',
                            narrow: '窄路',
                            wide: '宽路',
                            locked: '固定',
                            nearby: '附近避让',
                            attached: '依附',
                        }[name] || name,
                        name
                    )
                );
            }
            input.value = v;
            input.onchange = () => propertyValue(path, input.value);
        } else {
            input = document.createElement('input');
            input.type = typeof v === 'number' ? 'number' : 'text';
            input.value = v;
            if (typeof v === 'number') {
                input.step = '.1';
            }
            input.onchange = () => {
                if (
                    input.type === 'number' &&
                    (input.value === '' || !Number.isFinite(input.valueAsNumber))
                ) {
                    return;
                }
                propertyValue(path, input.type === 'number' ? input.valueAsNumber : input.value);
            };
        }
        input.setAttribute('aria-label', label);
        row.append(input);
        container.append(row);
    };
    build(value, [], root);
    renderQuickProperties(value);
    const spec = value?.rail_path || value?.beam_path || value?.floor_path || value?.connector;
    if (spec?.segments) {
        const button = document.createElement('button');
        button.textContent = '追加直线段';
        button.onclick = () => {
            const p = pendingPiece();
            const s = p.rail_path || p.beam_path || p.floor_path || p.connector;
            const last = s.segments.at(-1);
            const end = last.line || last.bezier?.at(-1);
            if (!end) {
                notify('公式曲线后请使用元素库插入下一段');
                return;
            }
            s.segments.push({ line: [end[0] + 20, end[1], end[2]] });
            $('piece').value = JSON.stringify(p, null, 2);
            documentAction('set');
        };
        root.append(button);
    }
}

// Presentation names never become document identities.
const names = {
    road: '直地面',
    rail: '直双轨',
    beam: '独木轨',
    rail_path: '自由双轨',
    beam_path: '自由单轨',
    floor_path: '自由地面',
    floor_polygon: '带洞平台',
    connector: '过渡连接',
    area: '地面平台',
    turn: '地面弯道',
    rail_turn: '双轨弯道',
    beam_turn: '单轨弯道',
    fan: '风扇升降台',
    tower: '风扇塔',
    cradle: '摇篮平台',
    trafo: '变球器',
    node: '分岔平台',
    fall: '落差轨道',
    log: '木柱路',
    trough: '凹槽路',
    walkway: '波浪步道',
    plank: '弓形木板',
    twist: '扭转桥',
    jump: '跳台',
    spiral: '螺旋轨',
    FanTower: '风扇塔',
    RotatingArm: '旋转臂',
    Drawbridge: '吊桥',
    SwitchBridge: '翻转桥',
    TiltingSlab: '翻板',
    Sack: '摆锤',
    Pusher: '推杆',
    Lift: '升降机',
    Swing: '摆桥',
    Flaps: '活动挡板',
    Plates: '活动板',
    Seesaw: '跷跷板',
    StoneBox: '石块机关',
    PaperChanger: '纸球变球器',
    StoneChanger: '石球变球器',
    WoodChanger: '木球变球器',
    FanBlock: '风扇升降台',
    Cradle: '摇篮平台',
    RailSplit: '分流轨道',
    Jump: '跳台',
    Twist: '扭转桥',
    ArchedPlank: '弓形木板',
    WaveWalkway: '波浪步道',
    LongWaveWalkway: '长波浪步道',
    Diamond: '菱形平台',
    Octagon: '八角平台',
    BoxMaze: '方块迷宫',
    PerforatedTrough: '带孔凹槽',
    Spiral: '螺旋轨',
    LogClimb: '攀爬木柱',
    LanternTower: '灯塔',
    Tower: '灯塔',
    Column: '支柱',
    Pillar: '支柱',
    FinishPole: '终点标柱',
};
const modules = {
    P_Modul_01: '推杆',
    P_Modul_03: '升降机',
    P_Modul_08: '摆桥',
    P_Modul_17: '旋转臂',
    P_Modul_19: '活动挡板',
    P_Modul_25: '吊桥',
    P_Modul_26: '摆锤',
    P_Modul_29: '活动板',
    P_Modul_30: '跷跷板',
    P_Modul_34: '石块机关',
    P_Modul_37: '翻转桥',
    P_Modul_41: '翻板',
};
function humanName(name) {
    if (names[name]) {
        return names[name];
    }
    const match = /^(Log|Trough)_(\d+)$/.exec(name || '');
    return match
        ? `${match[1] === 'Log' ? '木柱路' : '凹槽路'} ${match[2]}`
        : String(name || '零件')
              .replaceAll('_', ' ')
              .replaceAll('-', ' ');
}
function entityName(e) {
    if (!e) {
        return '选择一段轨道';
    }
    if (e.category === 'mechanism') {
        return modules[e.geometry_kind] || { P_Modul_18: '风扇' }[e.geometry_kind] || e.label;
    }
    const p = e.effective || e.piece || {};
    if (p.resource) {
        return humanName(p.resource);
    }
    if (p.module) {
        return modules[p.module] || '组合机关';
    }
    if (e.category === 'scenery') {
        return humanName(p.resource || e.details?.resource || 'Tower');
    }
    return names[e.geometry_kind] || e.label || '地图设置';
}
function paletteInfo(p) {
    const key = p.key;
    const resource = key.startsWith('resource:');
    return {
        title:
            key === 'curve'
                ? '可塑双轨'
                : key === 'beam'
                  ? '独木轨'
                  : key === 'platform'
                    ? '带洞平台'
                    : key === 'connector'
                      ? '过渡连接'
                      : resource
                        ? humanName(key.slice(9))
                        : key.startsWith('template:')
                          ? humanName(key.slice(9))
                          : p.label,
        group: key.startsWith('template:')
            ? '组合'
            : resource
              ? '机关与特殊路面'
              : ['road', 'floor', 'turn', 'platform'].includes(key)
                ? '地面'
                : '轨道',
        icon:
            key === 'curve'
                ? 'curve'
                : key.includes('turn')
                  ? 'turn'
                  : key === 'platform'
                    ? 'platform'
                    : resource
                      ? 'mechanism'
                      : ['road', 'floor'].includes(key)
                        ? 'floor'
                        : key === 'beam'
                          ? 'beam'
                          : 'rail',
    };
}
function thumbnail(kind) {
    const path =
        kind === 'curve'
            ? 'M12 44 C38 44 45 12 78 12'
            : kind === 'turn'
              ? 'M12 44 L38 44 Q64 44 64 18 L64 8'
              : 'M12 42 L78 14';
    let content = '';
    if (kind === 'platform') {
        content =
            '<path d="M9 30L52 7L83 27L40 50Z M40 30L52 22L64 29L52 37Z" fill="#618e82" fill-rule="evenodd" stroke="#b8e3d5"/>';
    } else if (kind === 'mechanism') {
        content =
            '<path d="M12 38L45 19L78 38L45 54Z" fill="#796543" stroke="#c7b58c"/><path d="M45 38V9M25 18L65 30M25 30L65 18" stroke="#d8c28f" stroke-width="4"/>';
    } else {
        if (kind === 'floor') {
            content += `<path d="${path}" stroke="#5c8279" stroke-width="18"/>`;
        }
        content += `<path d="${path}" stroke="#c0ddd6" stroke-width="${kind === 'beam' ? 5 : 3}"/>`;
        if (!['beam', 'floor'].includes(kind)) {
            content += `<path d="${path}" transform="translate(0 8)" stroke="#8eaaa4" stroke-width="3"/>`;
        }
    }
    return `<svg viewBox="0 0 90 60" aria-hidden="true" fill="none" stroke-linecap="round">${content}</svg>`;
}
function renderLibrary() {
    const root = $('partCards');
    root.replaceChildren();
    const draw = document.createElement('button');
    draw.className = 'draw-card';
    draw.textContent = '⌁  画一段双轨';
    draw.onclick = startSketch;
    root.append(draw);
    const search = $('partSearch').value.trim().toLowerCase();
    for (const group of ['轨道', '地面', '机关与特殊路面', '组合']) {
        const items = (state.palette || [])
            .map((p) => ({ ...p, ...paletteInfo(p) }))
            .filter(
                (p) => p.group === group && (!search || p.title.toLowerCase().includes(search))
            );
        if (!items.length) {
            continue;
        }
        const label = document.createElement('div');
        label.className = 'part-group';
        label.textContent = group;
        const grid = document.createElement('div');
        grid.className = 'part-grid';
        for (const p of items) {
            const b = document.createElement('button');
            b.className = 'part-card' + (placing === p.key ? ' active' : '');
            b.innerHTML = thumbnail(p.icon);
            const t = document.createElement('span');
            t.textContent = p.title;
            b.append(t);
            b.setAttribute('aria-label', '添加' + p.title);
            b.setAttribute('aria-pressed', placing === p.key ? 'true' : 'false');
            b.onclick = () => {
                if (busy || before) {
                    return;
                }
                sketch = null;
                $('sketchBar').hidden = true;
                placing = p.key;
                $('palette').value = p.key;
                setTool('select');
                renderLibrary();
                draw();
            };
            grid.append(b);
        }
        root.append(label, grid);
    }
}
function renderTree() {
    const root = $('mapTree');
    root.replaceChildren();
    for (let n = 1; n <= state.model.sectors; n++) {
        const section = document.createElement('details');
        section.className = 'tree-sector';
        section.open = n === currentEntity()?.sector;
        const title = document.createElement('summary');
        title.textContent = `第 ${n} 小节`;
        section.append(title);
        let index = 0;
        for (const e of state.model.entities
            .filter(
                (e) =>
                    e.sector === n &&
                    e.editable &&
                    !e.edit_owner &&
                    !['constraint', 'mechanism'].includes(e.category)
            )
            .flatMap((e) => [
                e,
                ...state.model.entities.filter(
                    (child) => child.category === 'mechanism' && child.parent === e.id
                ),
            ])) {
            const b = document.createElement('button');
            b.className = 'tree-item' + (e.id === selected ? ' active' : '');
            const count = document.createElement('span');
            count.className = 'tree-number';
            count.textContent = e.category === 'mechanism' ? '↳' : String(++index).padStart(2, '0');
            if (e.category === 'mechanism') {
                b.style.paddingLeft = '22px';
            }
            const label = document.createElement('span');
            label.textContent = entityName(e);
            b.append(count, label);
            if (e.source?.instances?.length) {
                const tag = document.createElement('small');
                tag.textContent = '组合';
                b.append(tag);
            }
            b.dataset.entity = e.id;
            b.onclick = (event) => selectEntity(e, true, event.shiftKey);
            section.append(b);
        }
        root.append(section);
    }
}
function selectEntity(e, focus = false) {
    if (busy) {
        return;
    }
    sketch = null;
    $('sketchBar').hidden = true;
    selected = e.id;
    livePiece = null;
    previewModel = null;
    $('category').value = 'all';
    if (Number($('sector').value) && Number($('sector').value) !== e.sector) {
        $('sector').value = e.sector;
        upload();
    }
    choose(focus);
    renderTree();
}
function dimension(
    root,
    label,
    value,
    change,
    { min = null, max = null, step = 0.5, note = '' } = {}
) {
    const block = document.createElement('div');
    block.className = 'dimension';
    const row = document.createElement('label');
    row.textContent = label;
    const input = document.createElement('input');
    input.type = 'number';
    input.step = step;
    if (min !== null) {
        input.min = min;
    }
    if (max !== null) {
        input.max = max;
    }
    input.value = Math.round(value * 1000) / 1000;
    input.setAttribute('aria-label', label);
    input.onchange = () => {
        if (input.value !== '' && Number.isFinite(input.valueAsNumber)) {
            change(input.valueAsNumber);
        }
    };
    input.onkeydown = (e) => {
        if (e.key === 'Enter') {
            input.blur();
        }
    };
    row.append(input);
    block.append(row);
    if (note) {
        const small = document.createElement('small');
        small.textContent = note;
        block.append(small);
    }
    root.append(block);
}
function setWholePiece(p) {
    $('piece').value = JSON.stringify(p, null, 2);
    livePiece = p;
    draw();
    documentAction('set');
}
function renderQuickProperties(p) {
    const root = $('quickProperties');
    root.replaceChildren();
    const e = currentEntity();
    $('title').textContent = entityName(e);
    $('selectionContext').textContent = e?.sector
        ? `第 ${e.sector} 小节 · ${e.source?.instances?.length ? '组合中的一个零件' : '所选零件'}`
        : '地图设置';
    $('phase').textContent = e?.source?.instances?.length
        ? '形状调整只作用于当前这一个。'
        : '尺寸调整后，后面的路线会跟随。';
    if (e?.category === 'mechanism') {
        const heading = document.createElement('h3');
        heading.textContent = '机关参数';
        root.append(heading);
        dimension(root, '激活小节', p.sector, (v) => propertyValue(['sector'], v), {
            step: 1,
            note: '决定游戏中何时启用和重置这个机关。',
        });
        dimension(
            root,
            '安装朝向（度）',
            e.details.yaw,
            (v) => propertyValue(['yaw'], v - e.details.base_pose.yaw),
            { step: 1 }
        );
        const owner = document.createElement('button');
        owner.textContent = '选择所属组合';
        owner.onclick = () =>
            selectEntity(
                state.model.entities.find((a) => a.id === e.parent),
                true
            );
        root.append(owner);
        const reset = document.createElement('button');
        reset.textContent = '恢复原始安装参数';
        reset.onclick = () =>
            setWholePiece({
                ...pendingPiece(),
                offset: [0, 0, 0],
                yaw: 0,
                sector: e.details.base_pose.sector,
            });
        root.append(reset);
        const schema = e.details.parameter_schema || {};
        if (Object.keys(schema).length) {
            const title = document.createElement('h3');
            title.textContent = '动力与运动';
            root.append(title);
            for (const [key, spec] of Object.entries(schema)) {
                const value = p.physics?.[key] ?? spec.default;
                if (typeof value === 'boolean') {
                    const label = document.createElement('label');
                    label.className = 'property';
                    label.textContent = spec.label;
                    const input = document.createElement('input');
                    input.type = 'checkbox';
                    input.checked = value;
                    input.setAttribute('aria-label', spec.label);
                    input.onchange = () => propertyValue(['physics', key], input.checked);
                    label.append(input);
                    root.append(label);
                } else {
                    dimension(
                        root,
                        spec.label + (spec.unit ? ' · ' + spec.unit : ''),
                        value,
                        (v) => propertyValue(['physics', key], v),
                        {
                            min: spec.min,
                            max: spec.max,
                            step: spec.step,
                            note:
                                key === 'rotor_speed'
                                    ? '正负值改变方向；转子速度与上升风力分别控制。'
                                    : key === 'angular_damping'
                                      ? '阻尼越大，转动衰减越快；被动机关没有恒定转速。'
                                      : '',
                        }
                    );
                }
            }
            const defaults = document.createElement('button');
            defaults.textContent = '恢复原版动力参数';
            defaults.onclick = () =>
                setWholePiece({
                    ...pendingPiece(),
                    physics: Object.fromEntries(
                        Object.entries(schema).map(([k, v]) => [k, v.default])
                    ),
                });
            root.append(defaults);
            const note = document.createElement('p');
            note.className = 'muted';
            note.textContent =
                '更改会生成此机关专用的原生行为资源，试玩时自动加载。导出游戏时需连同生成的 3D Entities 目录使用。';
            root.append(note);
        }
        return;
    }
    const section = document.createElement('h3');
    section.textContent = '尺寸与形状';
    root.append(section);
    const k = ['road', 'rail', 'beam'].find((k) => typeof p[k] === 'number');
    const pathKey = ['rail_path', 'beam_path', 'floor_path', 'connector'].find(
        (k) => p[k]?.segments
    );
    const segments = pathKey && p[pathKey].segments;
    if (k) {
        dimension(root, '长度', p[k], (v) => propertyValue([k], v), {
            note: '沿路线前进方向的长度。',
        });
        dimension(root, '下降高度', p.drop || 0, (v) => propertyValue(['drop'], v));
        if (['rail', 'beam'].includes(k)) {
            dimension(root, '拱起高度', p.hump || 0, (v) => propertyValue(['hump'], v));
        }
    } else if (segments?.length && segments.every((s) => s.bezier || s.line)) {
        const pts = segments.at(-1).bezier || [segments.at(-1).line];
        const end = pts.at(-1);
        const setEnd = (axis, value) => {
            const piece = pendingPiece();
            const segment = piece[pathKey].segments.at(-1);
            const points = segment.bezier || [segment.line];
            const last = points.at(-1);
            const delta = value - last[axis];
            if (axis === 0 && Math.abs(last[0]) > 0.001) {
                const ratio = value / last[0];
                for (const s of piece[pathKey].segments) {
                    for (const pt of s.bezier || [s.line]) {
                        pt[0] *= ratio;
                    }
                }
            } else {
                last[axis] = value;
                if (segment.bezier) {
                    points[1][axis] += delta;
                }
            }
            setWholePiece(piece);
        };
        dimension(root, '向前长度', end[0], (v) => setEnd(0, v), {
            note: '保持曲线比例，调整前后跨度。',
        });
        dimension(root, '横向偏移', end[1], (v) => setEnd(1, v), {
            note: '正值向右，负值向左；末端保持原来的朝向。',
        });
        dimension(root, '终点高差', end[2], (v) => setEnd(2, v), { note: '正值向上，负值向下。' });
    } else if (p.connector?.length !== undefined) {
        dimension(root, '过渡长度', p.connector.length, (v) =>
            propertyValue(['connector', 'length'], v)
        );
    } else if (p.at && Array.isArray(p.at)) {
        ['前后位置', '左右位置', '上下位置'].forEach((name, i) =>
            dimension(root, name, p.at[i], (v) => propertyValue(['at', i], v))
        );
    } else {
        const text = document.createElement('p');
        text.className = 'muted';
        text.textContent = segments
            ? '这段形状由多段曲线组成。使用画面中的控制点调整。'
            : '这个零件的专用选项在下方“精细参数”中。';
        root.append(text);
    }
    if (p.floor_path) {
        dimension(root, '路面宽度', p.floor_path.width || 5, (v) =>
            propertyValue(['floor_path', 'width'], v)
        );
    }
    const turnKey = ['turn', 'rail_turn', 'beam_turn'].find((k) =>
        ['left', 'right'].includes(p[k])
    );
    if (turnKey) {
        const pair = document.createElement('div');
        pair.className = 'toggle-pair';
        for (const [v, label] of [
            ['left', '向左转'],
            ['right', '向右转'],
        ]) {
            const b = document.createElement('button');
            b.textContent = label;
            b.className = p[turnKey] === v ? 'active' : '';
            b.onclick = () => propertyValue([turnKey], v);
            pair.append(b);
        }
        root.append(pair);
    }
    if (segments) {
        const note = document.createElement('div');
        note.className = 'inspector-note';
        note.textContent = '需要更自由的形状？直接拖动画面中的圆点。';
        const b = document.createElement('button');
        b.textContent = '调整曲线';
        b.onclick = () => {
            setTool('shape');
            focusSelection();
        };
        note.append(b);
        root.append(note);
    }
    if (e?.source?.instances?.length) {
        const note = document.createElement('div');
        note.className = 'inspector-note';
        note.textContent = '复制或删除会作用于整个组合；尺寸和塑形只修改当前零件。';
        root.append(note);
    }
}
function problemSummary(error) {
    if (!error) {
        return '';
    }
    if (error.includes('rail folds inside a bend')) {
        return '弯太急，双轨内侧重叠。请把弯拉宽，或减小左右起伏。';
    }
    if (error.includes('must end on a narrow road')) {
        return '这一小节还没有收尾：接一段直地面即可继续。';
    }
    if (error.includes('grid connection needs')) {
        return '连接处没有对齐。调整末端朝向，或接一段过渡连接。';
    }
    if (error.includes('cannot satisfy')) {
        return '有物件挡住了路线。请调整位置或形状，也可以撤销这一步。';
    }
    return '这一步需要调整才能生成地图。详情见“检查地图”。';
}
const originalPopulate = populate;
populate = function () {
    originalPopulate();
    $('palette').replaceChildren();
    for (const p of state.palette || []) {
        $('palette').add(new Option(paletteInfo(p).title, p.key));
    }
    if (placing) {
        $('palette').value = placing;
    }
    $('documentMessage').textContent = state.document_error
        ? problemSummary(state.document_error) + '\n草稿已保留；实体显示上次可用版本。'
        : '';
    $('problemDetail').textContent = state.document_error || '';
    $('undo').textContent = '↶ 撤销';
    $('redo').textContent = '↷ 重做';
    renderLibrary();
    renderTree();
    syncControls();
};
function syncControls() {
    if (!state) {
        return;
    }
    $('undo').disabled = busy || !state.undo_count;
    $('redo').disabled = busy || !state.redo_count;
    const e = currentEntity();
    const route = e?.source?.pointer?.startsWith('/sectors/') || e?.source?.instances?.length;
    for (const id of ['duplicate', 'delete']) {
        $(id).disabled = busy || !route || (typeof selection !== 'undefined' && selection.size > 1);
    }
    $('insert').disabled = busy || !placing || !route;
    const gaps = state.model.connections.open_connections?.length;
    $('play').disabled = busy || !!state.document_error || !!gaps;
    $('save').disabled = busy || !!state.document_error || !!gaps;
}
// Navigation stays available while geometry is updating.
work = async function (label, fn) {
    if (busy) {
        return;
    }
    busy = true;
    document
        .querySelectorAll(
            'aside input,aside select,aside textarea,aside button,.part-card,header button,#placement button,dialog button'
        )
        .forEach((b) => (b.disabled = true));
    notify(label);
    try {
        await fn();
        notify('');
    } catch (e) {
        notify(e.message);
    } finally {
        busy = false;
        document
            .querySelectorAll(
                'aside input,aside select,aside textarea,aside button,.part-card,header button,#placement button,dialog button'
            )
            .forEach((b) => (b.disabled = false));
        syncControls();
        if (typeof renderTransformControls === 'function') {
            renderTransformControls();
        }
        if (typeof syncNativePreview === 'function') {
            nativeSelection = '';
            syncNativePreview();
        }
    }
};
async function documentAction(action, customPiece = null) {
    if (before) {
        notify('请先返回当前地图再编辑');
        return;
    }
    if (busy) {
        return;
    }
    if (
        ['duplicate', 'delete'].includes(action) &&
        typeof selection !== 'undefined' &&
        selection.size > 1
    ) {
        notify('请单独选择一个零件后复制或删除');
        return;
    }
    await work('保存并更新地图…', async () => {
        const d = { action, entity: selected, revision: state.revision };
        if (action === 'set') {
            d.piece = pendingPiece();
            d.scope = $('scope').value;
        }
        if (['transform', 'snap'].includes(action)) {
            d.piece = customPiece;
        }
        if (action === 'insert') {
            d.preset = placing || $('palette').value;
            if (customPiece) {
                d.piece = customPiece;
            }
        }
        const previous = currentEntity();
        state = await api('/edit', d);
        if (state.selection) {
            selected = state.selection;
        } else if (action === 'delete') {
            const candidates = state.model.entities.filter(
                (e) => e.editable && e.sector === previous?.sector
            );
            selected = candidates.at(-1)?.id || selected;
        }
        previewModel = null;
        livePiece = null;
        oldScene = null;
        scene = await api('/scene');
        if (action === 'insert') {
            placing = null;
        }
        populate();
        upload();
        if (action === 'insert') {
            focusSelection();
        }
    });
}
for (const action of ['insert', 'duplicate', 'delete', 'undo', 'redo']) {
    $(action).onclick = () => documentAction(action);
}
$('apply').onclick = () => documentAction('set');
$('piece').oninput = () => {
    previewModel = null;
    livePiece = null;
    notify('配置尚未应用');
    draw();
};
function worldPoint(p, frame) {
    const [dx, dz] = frame.heading;
    const [x, y, z] = frame.position;
    return [x + p[0] * dx + p[1] * dz, y + p[2], z + p[0] * dz - p[1] * dx];
}
function curveData(piece, frame) {
    const key = ['connector', 'rail_path', 'beam_path', 'floor_path'].find(
        (k) => piece?.[k]?.segments
    );
    if (!key || !frame) {
        return { points: [], controls: [] };
    }
    const points = [];
    const controls = [];
    let start = [0, 0, 0];
    piece[key].segments.forEach((s, i) => {
        if (s.line) {
            points.push(worldPoint(start, frame), worldPoint(s.line, frame));
            controls.push({ path: [key, 'segments', i, 'line'], point: s.line, label: '终点' });
            start = s.line;
        } else if (s.bezier) {
            const [a, b, c] = s.bezier;
            for (let k = 0; k <= 32; k++) {
                const t = k / 32;
                const u = 1 - t;
                points.push(
                    worldPoint(
                        start.map(
                            (v, j) =>
                                u * u * u * v +
                                3 * u * u * t * a[j] +
                                3 * u * t * t * b[j] +
                                t * t * t * c[j]
                        ),
                        frame
                    )
                );
            }
            s.bezier.forEach((point, j) =>
                controls.push({
                    path: [key, 'segments', i, 'bezier', j],
                    point,
                    label:
                        j === 2
                            ? i === piece[key].segments.length - 1
                                ? '终点'
                                : '途经点 ' + (i + 1)
                            : ['起弯', '收弯'][j],
                })
            );
            start = c;
        }
    });
    return { points, controls };
}
function drawHandles() {
    handles = [];
    const e = currentEntity();
    if (!e || before || !['piece', 'instance'].includes($('scope').value) || !e.frame) {
        return;
    }
    let p;
    try {
        p = livePiece || pendingPiece();
    } catch {
        return;
    }
    const curve = curveData(p, e.frame);
    if (!curve.points.length) {
        return;
    }
    if (livePiece) {
        ctx.strokeStyle = '#a8f3d1';
        ctx.lineWidth = 4;
        ctx.beginPath();
        curve.points.forEach((v, i) => {
            const q = project(v);
            i ? ctx.lineTo(...q) : ctx.moveTo(...q);
        });
        ctx.stroke();
    }
    if (tool !== 'shape') {
        return;
    }
    handles = curve.controls;
    for (const h of handles) {
        h.screen = project(worldPoint(h.point, e.frame));
        ctx.fillStyle = '#ddb97a';
        ctx.strokeStyle = '#29372c';
        ctx.lineWidth = 3;
        ctx.beginPath();
        ctx.arc(...h.screen, 7, 0, 7);
        ctx.fill();
        ctx.stroke();
        ctx.fillStyle = '#f1dec0';
        ctx.font = '11px system-ui';
        ctx.fillText(h.label, h.screen[0] + 12, h.screen[1] - 10);
    }
}
function drawEditorOverlay() {
    drawSketch();
    if (typeof drawTransforms === 'function') {
        drawTransforms();
    }
    const e = currentEntity();
    appendTarget = null;
    const route = e?.source?.pointer?.startsWith('/sectors/') || e?.source?.instances?.length;
    $('placement').hidden = !placing || !!sketch;
    $('placementLabel').textContent = placing
        ? `添加${paletteInfo({ key: placing, label: state.palette.find((p) => p.key === placing)?.label }).title} · ${e?.sector ? '第 ' + e.sector + ' 小节' : ''}`
        : '';
    if (e && !before && route && tool !== 'shape' && !sketch) {
        const q = project(insertionAnchor(e).end.position);
        appendTarget = [q[0] + 25, q[1] - 23];
        ctx.strokeStyle = placing ? '#b8efd4' : '#769b89';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(...q);
        ctx.lineTo(...appendTarget);
        ctx.stroke();
        ctx.fillStyle = placing ? '#a2e4d0' : '#294038';
        ctx.beginPath();
        ctx.arc(...appendTarget, 13, 0, 7);
        ctx.fill();
        ctx.stroke();
        ctx.fillStyle = placing ? '#173c2f' : '#caf2de';
        ctx.font = '18px system-ui';
        ctx.textAlign = 'center';
        ctx.fillText('+', appendTarget[0], appendTarget[1] + 6);
        ctx.textAlign = 'start';
        ctx.font = '11px system-ui';
        ctx.fillStyle = '#bbd9c9';
        ctx.fillText(
            placing ? '点击接上' : '从这里接续',
            appendTarget[0] + 19,
            appendTarget[1] + 4
        );
    }
    $('hint').textContent = sketch
        ? '点击放置途经点 · Enter 完成 · Backspace 退回一点 · Esc 取消 · 右键平移'
        : placing
          ? '亮起的 + 是接续位置 · 点击 + 或“接在这里”放置 · Esc 取消'
          : tool === 'shape'
            ? `${vertical ? '上下拖动调整高度' : '拖动圆点调整平面形状'} · Esc 取消本次拖动 · 滚轮缩放`
            : tool === 'move'
              ? '拖动 X / Y / Z 轴移动 · 中心点沿水平面移动 · Shift 点击多选 · Esc 取消'
              : tool === 'rotate'
                ? '拖动圆环整体旋转 · Alt 精细转动自由形状 · Esc 取消'
                : tool === 'pan'
                  ? '拖动画面平移 · 滚轮缩放 · V 返回选择'
                  : tool === 'orbit'
                    ? '拖动画面旋转 · 滚轮缩放 · V 返回选择'
                    : '点击轨道选择 · 点击 + 继续搭建 · 右键拖动平移 · 滚轮缩放 · F 聚焦';
    $('viewBadge').textContent =
        (pitch > 1.5 ? '俯视' : '立体') +
        ' · ' +
        (Number($('sector').value) ? '第 ' + $('sector').value + ' 小节' : '整张地图');
    syncControls();
}
function setTool(next) {
    tool = next;
    for (const key of ['select', 'shape', 'pan', 'orbit']) {
        $('tool-' + key).classList.toggle('active', key === tool);
        $('tool-' + key).setAttribute('aria-pressed', String(key === tool));
    }
    $('shapeBar').hidden = tool !== 'shape';
    overlay.style.cursor =
        tool === 'pan'
            ? 'grab'
            : tool === 'orbit'
              ? 'move'
              : tool === 'shape'
                ? 'crosshair'
                : 'default';
    draw();
}
for (const key of ['select', 'shape', 'pan', 'orbit']) {
    $('tool-' + key).onclick = () => setTool(key);
}
for (const [id, value] of [
    ['plane', false],
    ['height', true],
]) {
    $(id).onclick = () => {
        vertical = value;
        $('plane').classList.toggle('active', !vertical);
        $('height').classList.toggle('active', vertical);
        draw();
    };
}
function fitPoints(points) {
    if (!points.length) {
        return;
    }
    const lo = [Infinity, Infinity, Infinity];
    const hi = [-Infinity, -Infinity, -Infinity];
    for (const p of points) {
        for (let i = 0; i < 3; i++) {
            lo[i] = Math.min(lo[i], p[i]);
            hi[i] = Math.max(hi[i], p[i]);
        }
    }
    center = lo.map((v, i) => (v + hi[i]) / 2);
    const projected = points.map(project);
    const xs = projected.map((p) => p[0]);
    const ys = projected.map((p) => p[1]);
    scale = Math.max(
        0.1,
        Math.min(
            18,
            scale *
                Math.min(
                    (overlay.clientWidth - 180) / Math.max(80, Math.max(...xs) - Math.min(...xs)),
                    (overlay.clientHeight - 220) / Math.max(80, Math.max(...ys) - Math.min(...ys))
                )
        )
    );
    draw();
}
fit = function () {
    const sec = Number($('sector').value);
    fitPoints(
        currentModel()
            .entities.filter(
                (e) => !e.global && (!sec || e.sector === sec) && e.category === 'geometry'
            )
            .flatMap((e) => (e.path?.length ? e.path : [e.end.position]))
    );
};
function focusSelection() {
    const e = currentEntity();
    if (!e || e.global) {
        return;
    }
    fitPoints(e.path?.length ? e.path : [e.end.position]);
}
const originalChoose = choose;
choose = function (focus = false) {
    originalChoose(false);
    if (focus) {
        focusSelection();
    }
    syncControls();
};
$('focus').onclick = focusSelection;
$('fit').onclick = fit;
$('sector').onchange = () => {
    const n = Number($('sector').value);
    const e = state.model.entities.find(
        (e) => e.editable && e.sector === n && e.category === 'geometry'
    );
    if (e) {
        selectEntity(e);
    }
    upload();
    fit();
};
$('decorations').onchange = upload;
function startEditor() {
    $('sector').value = String(currentEntity()?.sector || 1);
    upload();
    fit();
}
function pick(x, y) {
    let best = null;
    for (const h of hit) {
        const path = h.e.path || [];
        const segs =
            path.length > 1
                ? path.slice(1).map((b, i) => [path[i], b])
                : [[h.e.end.position, h.e.end.position]];
        for (const [pa, pb] of segs) {
            const a = project(pa);
            const b = project(pb);
            const dx = b[0] - a[0];
            const dy = b[1] - a[1];
            const t = Math.max(
                0,
                Math.min(1, ((x - a[0]) * dx + (y - a[1]) * dy) / (dx * dx + dy * dy || 1))
            );
            const dist = Math.hypot(x - a[0] - t * dx, y - a[1] - t * dy);
            const radius =
                h.e.geometry_kind?.includes('floor') || ['road', 'area'].includes(h.e.geometry_kind)
                    ? Math.max(10, 2.5 * scale)
                    : 10;
            if (dist < radius && (!best || dist < best.dist)) {
                best = { e: h.e, dist };
            }
        }
    }
    return best?.e;
}
function movePlane(dx, dy) {
    const [r, u] = basis();
    const a = dx / scale;
    const b = -dy / scale;
    const det = r[0] * u[2] - r[2] * u[0];
    return Math.abs(det) < 1e-6
        ? [0, 0]
        : [(a * u[2] - b * r[2]) / det, (b * r[0] - a * u[0]) / det];
}
overlay.onpointerdown = (e) => {
    overlay.focus();
    if (e.button === 2 || tool === 'pan' || tool === 'orbit') {
        viewDrag = { x: e.clientX, y: e.clientY, pan: e.button === 2 || tool === 'pan' };
        overlay.setPointerCapture(e.pointerId);
        return;
    }
    if (busy || before) {
        return;
    }
    if (sketch) {
        const p = sketchPoint(e.offsetX, e.offsetY);
        if (Math.hypot(...p.map((v, i) => v - (sketch.points.at(-1) || [0, 0, 0])[i])) > 0.5) {
            sketch.points.push(p);
        }
        sketch.hover = null;
        draw();
        return;
    }
    if (appendTarget && Math.hypot(appendTarget[0] - e.offsetX, appendTarget[1] - e.offsetY) < 17) {
        if (placing) {
            documentAction('insert');
        } else {
            $('partsTab').click();
            $('partSearch').focus();
            notify('从左边选择要接上的零件。');
        }
        return;
    }
    const h =
        tool === 'shape' &&
        handles.find((h) => Math.hypot(h.screen[0] - e.offsetX, h.screen[1] - e.offsetY) < 13);
    if (h) {
        handleDrag = {
            path: h.path,
            x: e.clientX,
            y: e.clientY,
            piece: pendingPiece(),
            frame: currentEntity().frame,
        };
        handleDrag.initial = atPath(handleDrag.piece, h.path).slice();
        overlay.setPointerCapture(e.pointerId);
        return;
    }
    const found = pick(e.offsetX, e.offsetY);
    if (found) {
        selectEntity(found, false, e.shiftKey);
    }
};
overlay.onpointermove = (e) => {
    if (viewDrag) {
        const d = viewDrag;
        const dx = e.clientX - d.x;
        const dy = e.clientY - d.y;
        d.x = e.clientX;
        d.y = e.clientY;
        if (d.pan) {
            const [r, u] = basis();
            center = center.map((v, i) => v - (dx * r[i]) / scale + (dy * u[i]) / scale);
        } else {
            yaw -= dx * 0.006;
            pitch = Math.max(0.15, Math.min(1.57, pitch + dy * 0.006));
        }
        draw();
        return;
    }
    if (sketch) {
        sketch.hover = sketchPoint(e.offsetX, e.offsetY);
        draw();
        return;
    }
    if (!handleDrag) {
        return;
    }
    const h = handleDrag;
    const dx = e.clientX - h.x;
    const dy = e.clientY - h.y;
    const point = atPath(h.piece, h.path);
    const oldPoint = point.slice();
    const snap = $('snap').checked
        ? (v) => Math.round(v * 2) / 2
        : (v) => Math.round(v * 1000) / 1000;
    if (vertical) {
        point[2] = snap(h.initial[2] - dy / (scale * Math.max(0.25, Math.cos(pitch))));
    } else {
        const [wx, wz] = movePlane(dx, dy);
        const [hx, hz] = h.frame.heading;
        point[0] = snap(h.initial[0] + wx * hx + wz * hz);
        point[1] = snap(h.initial[1] + wx * hz - wz * hx);
    }
    if (h.path.at(-2) === 'bezier' && h.path.at(-1) === 2) {
        const segments = h.piece[h.path[0]].segments;
        const index = h.path[2];
        const delta = point.map((v, i) => v - oldPoint[i]);
        for (const neighbor of [segments[index].bezier[1], segments[index + 1]?.bezier?.[0]]) {
            if (neighbor) {
                for (let i = 0; i < 3; i++) {
                    neighbor[i] += delta[i];
                }
            }
        }
    }
    $('piece').value = JSON.stringify(h.piece, null, 2);
    livePiece = h.piece;
    h.changed = true;
    draw();
};
overlay.onpointerup = () => {
    viewDrag = null;
    if (!handleDrag) {
        return;
    }
    const changed = handleDrag.changed;
    handleDrag = null;
    if (changed) {
        documentAction('set');
    }
};
function cancelGesture() {
    sketch = null;
    $('sketchBar').hidden = true;
    viewDrag = null;
    if (handleDrag) {
        handleDrag = null;
        livePiece = null;
        setScope();
    }
    placing = null;
    renderLibrary();
    draw();
}
overlay.onpointercancel = cancelGesture;
overlay.ondblclick = (e) => {
    if (!sketch) {
        const found = pick(e.offsetX, e.offsetY);
        if (found) {
            selectEntity(found, true);
        }
    }
};
$('cancelInsert').onclick = () => {
    placing = null;
    renderLibrary();
    draw();
};
$('partSearch').oninput = renderLibrary;
for (const [tabId, panelId, otherId] of [
    ['partsTab', 'partsPanel', 'mapPanel'],
    ['mapTab', 'mapPanel', 'partsPanel'],
]) {
    $(tabId).onclick = () => {
        $(panelId).hidden = false;
        $(otherId).hidden = true;
        $('partsTab').classList.toggle('active', panelId === 'partsPanel');
        $('mapTab').classList.toggle('active', panelId === 'mapPanel');
    };
}
$('showChecks').onclick = () => $('checksDialog').showModal();
$('closeChecks').onclick = () => $('checksDialog').close();
$('play').onclick = () => $('playDialog').showModal();
$('closePlay').onclick = () => $('playDialog').close();
$('manualPlay').onclick = () =>
    work('打开游戏…', async () => {
        await api('/launch', { revision: state.revision, sector: chosenSector(), handoff: 1 });
        $('playDialog').close();
    });
$('entities').onchange = () => {
    const e = state.model.entities.find((e) => e.id === $('entities').value);
    selectEntity(e, true);
};
document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
        cancelGesture();
        return;
    }
    if (
        ['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName) ||
        document.querySelector('dialog[open]')
    ) {
        return;
    }
    const key = e.key.toLowerCase();
    if (sketch) {
        if (e.key === 'Enter') {
            e.preventDefault();
            finishSketch();
        }
        if (e.key === 'Backspace') {
            e.preventDefault();
            sketch.points.pop();
            draw();
        }
        return;
    }
    if ((e.metaKey || e.ctrlKey) && key === 'z') {
        e.preventDefault();
        documentAction(e.shiftKey ? 'redo' : 'undo');
    } else if ((e.metaKey || e.ctrlKey) && key === 'd') {
        e.preventDefault();
        documentAction('duplicate');
    } else if (e.key === 'Delete' || e.key === 'Backspace') {
        e.preventDefault();
        documentAction('delete');
    } else if (!e.metaKey && !e.ctrlKey) {
        if ({ v: 'select', p: 'shape', h: 'pan', o: 'orbit' }[key]) {
            setTool({ v: 'select', p: 'shape', h: 'pan', o: 'orbit' }[key]);
        }
        if (key === 'f') {
            focusSelection();
        }
        if (e.key === 'Home') {
            fit();
        }
    }
});

function insertionAnchor(e) {
    const call = e.source?.instances?.[0]?.id;
    if (!call) {
        return e;
    }
    return (
        state.model.entities
            .filter(
                (p) =>
                    p.source?.instances?.[0]?.id === call &&
                    !p.edit_owner &&
                    ['geometry', 'resource'].includes(p.category)
            )
            .at(-1) || e
    );
}
function startSketch() {
    if (busy || before) {
        return;
    }
    const e = currentEntity();
    if (!e?.source?.pointer?.startsWith('/sectors/') && !e?.source?.instances?.length) {
        notify('请先选择一段路线，作为绘制的起点。');
        return;
    }
    const anchor = insertionAnchor(e);
    placing = null;
    setTool('select');
    sketch = {
        frame: { position: anchor.end.position.slice(), heading: anchor.end.heading.slice() },
        points: [],
        hover: null,
    };
    pitch = Math.PI / 2;
    yaw = 0;
    center = anchor.end.position.slice();
    scale = 5;
    $('sketchBar').hidden = false;
    $('finishSketch').disabled = true;
    overlay.focus();
    draw();
}
function sketchPoint(x, y) {
    const start = project(sketch.frame.position);
    const [wx, wz] = movePlane(x - start[0], y - start[1]);
    const [hx, hz] = sketch.frame.heading;
    return [Math.round((wx * hx + wz * hz) * 2) / 2, Math.round((wx * hz - wz * hx) * 2) / 2, 0];
}

function drawSketch() {
    if (!sketch) {
        return;
    }
    const points = sketch.points.slice();
    if (
        sketch.hover &&
        (!points.length || Math.hypot(...sketch.hover.map((v, i) => v - points.at(-1)[i])) > 0.1)
    ) {
        points.push(sketch.hover);
    }
    const data = curveData(sketchPiece(points), sketch.frame);
    ctx.strokeStyle = '#9de7cc';
    ctx.lineWidth = 4;
    ctx.setLineDash([8, 5]);
    ctx.beginPath();
    data.points.forEach((p, i) => {
        const q = project(p);
        i ? ctx.lineTo(...q) : ctx.moveTo(...q);
    });
    ctx.stroke();
    ctx.setLineDash([]);
    for (const [i, p] of [[0, 0, 0], ...sketch.points].entries()) {
        const q = project(worldPoint(p, sketch.frame));
        ctx.fillStyle = '#c2f5df';
        ctx.beginPath();
        ctx.arc(...q, 6, 0, 7);
        ctx.fill();
        ctx.font = '12px system-ui';
        ctx.fillText(i ? String(i) : '起点', q[0] + 12, q[1] - 9);
    }
    $('finishSketch').disabled = busy || !sketch.points.length;
    $('backSketch').disabled = !sketch.points.length;
}
async function finishSketch() {
    if (!sketch?.points.length || busy) {
        return;
    }
    const piece = sketchPiece(sketch.points);
    sketch = null;
    $('sketchBar').hidden = true;
    placing = 'curve';
    await documentAction('insert', piece);
    setTool('shape');
}
$('finishSketch').onclick = finishSketch;
$('backSketch').onclick = () => {
    if (sketch) {
        sketch.points.pop();
        sketch.hover = null;
        draw();
    }
};
$('cancelSketch').onclick = cancelGesture;

function drawEditorGrid() {
    if (pitch < 1.5 || (!sketch && tool !== 'shape')) {
        return;
    }
    const step = [1, 2, 5, 10, 20, 50, 100, 500, 1000].find((v) => v * scale >= 25) || 1000;
    const extent = Math.max(overlay.clientWidth, overlay.clientHeight) / scale;
    const origin = sketch?.frame.position || currentEntity()?.frame?.position || center;
    ctx.strokeStyle = '#95bdac18';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let d = -Math.ceil(extent / step) * step; d <= extent; d += step) {
        for (const [a, b] of [
            [
                [origin[0] + d, origin[1], center[2] - extent],
                [origin[0] + d, origin[1], center[2] + extent],
            ],
            [
                [center[0] - extent, origin[1], origin[2] + d],
                [center[0] + extent, origin[1], origin[2] + d],
            ],
        ]) {
            ctx.moveTo(...project(a));
            ctx.lineTo(...project(b));
        }
    }
    ctx.stroke();
    ctx.fillStyle = '#94b4a3';
    ctx.font = '10px system-ui';
    ctx.fillText(`网格间距 ${step} 单位`, 90, overlay.clientHeight - 65);
}
