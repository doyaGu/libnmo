/* Selection, rigid placement and explicit compatible-port docking. */
let selection = new Set();
let transformDrag = null;
let transformPreview = null;
let boxDrag = null;
let gizmos = [];
let snapChoice = null;
function selectionHas(id) {
    return selection.size ? selection.has(id) : id === selected;
}
function transformable(e) {
    return (
        e &&
        e.editable &&
        (e.entry ||
            e.edit_targets?.placement ||
            e.edit_targets?.mechanism ||
            e.details?.objects?.length)
    );
}
function selectionItems() {
    return state.model.entities.filter(
        (e) => selectionHas(e.id) && transformable(e) && !e.edit_owner
    );
}
function selectionPivot() {
    const points = selectionItems().map((e) =>
        e.entry ? e.entry.position.map((v, i) => (v + e.end.position[i]) / 2) : e.end.position
    );
    return points.length
        ? [0, 1, 2].map((i) => points.reduce((s, p) => s + p[i], 0) / points.length)
        : [0, 0, 0];
}
function rotateWorld(p, angle) {
    const c = Math.cos((angle * Math.PI) / 180);
    const s = Math.sin((angle * Math.PI) / 180);
    return [p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c];
}
function transformedPoint(p, operation) {
    const v = rotateWorld(
        p.map((v, i) => v - operation.pivot[i]),
        operation.angle || 0
    );
    return v.map((v, i) => v + operation.pivot[i] + operation.delta[i]);
}
const baseSelectEntity = selectEntity;
selectEntity = function (e, focus = false, add = false) {
    if (!e || busy) {
        return;
    }
    if (e.edit_owner) {
        e = state.model.entities.find((a) => a.id === e.edit_owner) || e;
    }
    if (e.id.startsWith('editor/rule/')) {
        e = state.model.entities.find((a) => a.id === e.id.slice(12)) || e;
    }
    if (add && transformable(e)) {
        if (selection.has(e.id) && selection.size > 1) {
            selection.delete(e.id);
            e = state.model.entities.find((a) => a.id === [...selection].at(-1));
        } else {
            selection.add(e.id);
        }
    } else {
        selection = new Set([e.edit_owner || e.id]);
    }
    transformPreview = null;
    snapChoice = null;
    $('snapPanel').hidden = true;
    baseSelectEntity(e, focus);
    renderTransformControls();
};
const baseChooseSpatial = choose;
choose = function (focus = false) {
    if (!selection.has(selected)) {
        selection = new Set([selected]);
    }
    selection = new Set(
        [...selection].filter((id) => state.model.entities.some((e) => e.id === id))
    );
    baseChooseSpatial(focus);
    if (state) {
        renderTransformControls();
    }
};
const basePopulateSpatial = populate;
populate = function () {
    basePopulateSpatial();
    const gaps = state.model.connections.open_connections || [];
    if (!state.document_error && gaps.length) {
        $('documentMessage').textContent =
            `${gaps.length} 处连接已断开。草稿已保存；用“连接吸附”对齐后即可试玩。`;
    }
    renderTransformControls();
};
function renderTransformControls() {
    const root = $('transformControls');
    root.replaceChildren();
    const items = selectionItems();
    $('selectionCount').textContent = items.length > 1 ? `已选择 ${items.length} 个零件` : '';
    if (!items.length) {
        return;
    }
    const pivot = selectionPivot();
    if (items.length > 1) {
        $('title').textContent = items.length + ' 个零件';
        $('phase').textContent = '整体移动或旋转，保持相互位置。Shift 点击增减选择。';
        $('quickProperties').hidden = true;
        $('precise').hidden = true;
    } else {
        $('quickProperties').hidden = false;
        $('precise').hidden = false;
        if (state.model.free_layout) {
            $('phase').textContent = '自由布局：其他零件保持原位，接头可用吸附对齐。';
        }
    }
    const title = document.createElement('h3');
    title.textContent = items.length > 1 ? '组中心 · 世界坐标' : '位置 · 世界坐标';
    root.append(title);
    const row = document.createElement('div');
    row.className = 'world-position';
    ['X', 'Y', 'Z'].forEach((name, i) => {
        const label = document.createElement('label');
        label.textContent = name + (i === 1 ? ' 高度' : '');
        const input = document.createElement('input');
        input.type = 'number';
        input.step = '.5';
        input.value = Math.round(pivot[i] * 1000) / 1000;
        input.setAttribute('aria-label', '世界位置 ' + name);
        input.onchange = () => {
            if (!Number.isFinite(input.valueAsNumber)) {
                return;
            }
            const delta = [0, 0, 0];
            delta[i] = input.valueAsNumber - pivot[i];
            commitTransform({ ids: items.map((e) => e.id), pivot, delta, angle: 0 });
        };
        input.onkeydown = (e) => {
            if (e.key === 'Enter') {
                input.blur();
            }
        };
        label.append(input);
        row.append(label);
    });
    root.append(row);
    const tools = document.createElement('div');
    tools.className = 'row';
    for (const [key, label] of [
        ['move', '移动 G'],
        ['rotate', '旋转 R'],
    ]) {
        const b = document.createElement('button');
        b.textContent = label;
        b.onclick = () => setTool(key);
        tools.append(b);
    }
    root.append(tools);
    const native = items.some(
        (e) =>
            e.entry &&
            !['rail_path', 'floor_path', 'floor_polygon', 'connector'].includes(e.geometry_kind)
    );
    dimension(
        root,
        '绕中心旋转 °',
        0,
        (angle) => commitTransform({ ids: items.map((e) => e.id), pivot, delta: [0, 0, 0], angle }),
        {
            step: native ? 90 : 1,
            note: native ? '包含原版网格零件：按 90° 转动。' : '所选对象可按任意角度转动。',
        }
    );
    if (!items.some((e) => e.ports?.snappable)) {
        return;
    }
    const snap = document.createElement('button');
    snap.className = 'snap-button';
    snap.textContent = '连接吸附…';
    snap.disabled = busy || !items.some((e) => e.ports?.snappable);
    snap.onclick = openSnap;
    root.append(snap);
    const auto = document.createElement('label');
    auto.className = 'auto-snap';
    auto.innerHTML = '<input type="checkbox" id="autoSnap">移动后吸附近处接口（2 单位）';
    auto.querySelector('input').checked = autoSnapEnabled;
    auto.querySelector('input').onchange = (e) => {
        autoSnapEnabled = e.target.checked;
    };
    root.append(auto);
    for (const button of document.querySelectorAll('.tree-item')) {
        button.classList.toggle('active', selection.has(button.dataset.entity));
    }
}
let autoSnapEnabled = true;
async function commitTransform(operation) {
    if (busy) {
        return;
    }
    transformPreview = null;
    await documentAction('transform', operation);
    draw();
}
const baseToolSpatial = setTool;
setTool = function (next) {
    transformPreview = null;
    transformDrag = null;
    baseToolSpatial(next);
    for (const key of ['move', 'rotate']) {
        $('tool-' + key).classList.toggle('active', tool === key);
        $('tool-' + key).setAttribute('aria-pressed', String(tool === key));
    }
    overlay.style.cursor = ['move', 'rotate'].includes(tool) ? 'crosshair' : overlay.style.cursor;
};
$('tool-move').onclick = () => setTool('move');
$('tool-rotate').onclick = () => setTool('rotate');
function drawTransforms() {
    gizmos = [];
    const items = selectionItems();
    if (!items.length) {
        return;
    }
    const pivot = selectionPivot();
    if (transformPreview) {
        ctx.strokeStyle = '#f4cd84';
        ctx.lineWidth = 3;
        ctx.setLineDash([7, 4]);
        for (const e of items) {
            const path = e.path?.length ? e.path : [e.end.position];
            ctx.beginPath();
            path.forEach((p, i) => {
                const q = project(transformedPoint(p, transformPreview));
                i ? ctx.lineTo(...q) : ctx.moveTo(...q);
            });
            ctx.stroke();
        }
        ctx.setLineDash([]);
    }
    if (boxDrag) {
        ctx.strokeStyle = '#a1eed2';
        ctx.fillStyle = '#84e6bd15';
        ctx.fillRect(boxDrag.x, boxDrag.y, boxDrag.toX - boxDrag.x, boxDrag.toY - boxDrag.y);
        ctx.strokeRect(boxDrag.x, boxDrag.y, boxDrag.toX - boxDrag.x, boxDrag.toY - boxDrag.y);
    }
    if (snapChoice) {
        const q = project(snapChoice.position);
        ctx.strokeStyle = '#a7ffbb';
        ctx.lineWidth = 3;
        ctx.beginPath();
        ctx.arc(...q, 12, 0, 7);
        ctx.stroke();
    }
    if (!['move', 'rotate'].includes(tool) || before || sketch) {
        return;
    }
    const at = project(transformPreview ? transformedPoint(pivot, transformPreview) : pivot);
    ctx.fillStyle = '#eefde5';
    ctx.beginPath();
    ctx.arc(...at, 7, 0, 7);
    ctx.fill();
    if (tool === 'rotate') {
        ctx.strokeStyle = '#e8c480';
        ctx.lineWidth = 3;
        ctx.beginPath();
        ctx.arc(...at, 55, 0, 7);
        ctx.stroke();
        gizmos.push({ kind: 'rotate', at });
        return;
    }
    gizmos.push({ kind: 'plane', at });
    for (const [axis, color] of [
        [0, '#ef9e88'],
        [1, '#aedb91'],
        [2, '#8abfe9'],
    ]) {
        const p = pivot.slice();
        p[axis] += 1;
        let direction = project(p).map((v, i) => v - project(pivot)[i]);
        let len = Math.hypot(...direction);
        if (len < 0.1) {
            direction = [0, -1];
            len = 1;
        }
        const to = at.map((v, i) => v + (direction[i] / len) * 65);
        ctx.strokeStyle = color;
        ctx.lineWidth = 3;
        ctx.beginPath();
        ctx.moveTo(...at);
        ctx.lineTo(...to);
        ctx.stroke();
        ctx.fillStyle = color;
        ctx.fillRect(to[0] - 5, to[1] - 5, 10, 10);
        ctx.font = '12px system-ui';
        ctx.fillText('XYZ'[axis], to[0] + 9, to[1] - 8);
        gizmos.push({ kind: 'axis', axis, at: to, direction });
    }
}
const basePointerDownSpatial = overlay.onpointerdown;
const basePointerMoveSpatial = overlay.onpointermove;
const basePointerUpSpatial = overlay.onpointerup;
overlay.onpointerdown = (e) => {
    if (busy || before || sketch || e.button !== 0) {
        return basePointerDownSpatial(e);
    }
    if (['move', 'rotate'].includes(tool)) {
        const at = [e.offsetX, e.offsetY];
        const g = gizmos.find((g) =>
            g.kind === 'rotate'
                ? Math.abs(Math.hypot(at[0] - g.at[0], at[1] - g.at[1]) - 55) < 12
                : Math.hypot(at[0] - g.at[0], at[1] - g.at[1]) < 14
        );
        if (g) {
            const pivot = selectionPivot();
            transformDrag = {
                ...g,
                x: e.clientX,
                y: e.clientY,
                pivot,
                ids: selectionItems().map((e) => e.id),
                startAngle: Math.atan2(e.offsetY - g.at[1], e.offsetX - g.at[0]),
            };
            overlay.setPointerCapture(e.pointerId);
            overlay.focus();
            return;
        }
    }
    if (
        tool === 'select' &&
        !pick(e.offsetX, e.offsetY) &&
        !(appendTarget && Math.hypot(e.offsetX - appendTarget[0], e.offsetY - appendTarget[1]) < 18)
    ) {
        boxDrag = { x: e.offsetX, y: e.offsetY, toX: e.offsetX, toY: e.offsetY, add: e.shiftKey };
        overlay.setPointerCapture(e.pointerId);
        overlay.focus();
        return;
    }
    basePointerDownSpatial(e);
};
overlay.onpointermove = (e) => {
    if (boxDrag) {
        boxDrag.toX = e.offsetX;
        boxDrag.toY = e.offsetY;
        draw();
        return;
    }
    if (!transformDrag) {
        return basePointerMoveSpatial(e);
    }
    const d = transformDrag;
    const dx = e.clientX - d.x;
    const dy = e.clientY - d.y;
    const operation = { ids: d.ids, pivot: d.pivot, delta: [0, 0, 0], angle: 0 };
    if (d.kind === 'rotate') {
        let angle =
            ((Math.atan2(e.offsetY - d.at[1], e.offsetX - d.at[0]) - d.startAngle) * 180) / Math.PI;
        const step = selectionItems().some(
            (e) =>
                e.entry &&
                !['rail_path', 'floor_path', 'floor_polygon', 'connector'].includes(e.geometry_kind)
        )
            ? 90
            : e.altKey
              ? 1
              : 15;
        operation.angle = Math.round(angle / step) * step;
    } else if (d.kind === 'axis') {
        const p = d.pivot.slice();
        p[d.axis] += 1;
        const projected = project(p).map((v, i) => v - project(d.pivot)[i]);
        const den = dot(projected, projected);
        operation.delta[d.axis] =
            Math.round(
                (den > 0.01 ? (dx * projected[0] + dy * projected[1]) / den : -dy / scale) * 2
            ) / 2;
    } else {
        const [x, z] = movePlane(dx, dy);
        operation.delta = [Math.round(x * 2) / 2, 0, Math.round(z * 2) / 2];
    }
    transformPreview = operation;
    draw();
};
overlay.onpointerup = async (e) => {
    if (boxDrag) {
        const b = boxDrag;
        boxDrag = null;
        const ids = state.model.entities
            .filter(
                (a) =>
                    transformable(a) &&
                    !a.edit_owner &&
                    (!Number($('sector').value) || a.sector === Number($('sector').value)) &&
                    (a.path?.length ? a.path : [a.end.position]).some((p) => {
                        const [x, y] = project(p);
                        return (
                            x >= Math.min(b.x, b.toX) &&
                            x <= Math.max(b.x, b.toX) &&
                            y >= Math.min(b.y, b.toY) &&
                            y <= Math.max(b.y, b.toY)
                        );
                    })
            )
            .map((a) => a.id);
        if (ids.length) {
            selection = new Set([...(b.add ? selection : []), ...ids]);
            selected = ids[0];
            choose();
            renderTree();
            renderTransformControls();
        }
        draw();
        return;
    }
    if (transformDrag) {
        const operation = transformPreview;
        transformDrag = null;
        transformPreview = null;
        if (operation && (operation.angle || operation.delta.some(Boolean))) {
            if (autoSnapEnabled && !operation.angle) {
                operation.snap_distance = 2;
            }
            await commitTransform(operation);
        }
        draw();
        return;
    }
    basePointerUpSpatial(e);
};
function cancelTransform() {
    transformDrag = null;
    transformPreview = null;
    boxDrag = null;
    snapChoice = null;
    $('snapPanel').hidden = true;
    draw();
}
overlay.addEventListener('pointercancel', cancelTransform);
async function openSnap() {
    await work('寻找兼容的连接…', async () => {
        const ids = selectionItems().map((e) => e.id);
        const options = await api('/snap-candidates', { ids, revision: state.revision });
        $('snapOptions').replaceChildren();
        $('snapPanel').hidden = false;
        $('confirmSnap').disabled = true;
        for (const option of options) {
            const b = document.createElement('button');
            const target = state.model.entities.find((e) => e.id === option.target.entity);
            b.textContent = `${option.source.port === 'entry' ? '入口' : '出口'} → 第 ${target.sector} 节 ${entityName(target)}${option.target.port === 'entry' ? '入口' : '出口'} · ${option.distance.toFixed(1)}`;
            b.onclick = () => {
                snapChoice = option;
                const source = state.model.entities.find((e) => e.id === option.source.entity)
                    .ports[option.source.port];
                transformPreview = {
                    ids,
                    pivot: source.position,
                    delta: option.position.map((v, i) => v - source.position[i]),
                    angle: option.angle,
                };
                $('confirmSnap').disabled = false;
                draw();
            };
            $('snapOptions').append(b);
        }
        if (!options.length) {
            $('snapOptions').textContent =
                '没有截面兼容的连接。轨距、宽度、砖边截面和倾角必须匹配。';
        }
    });
    $('confirmSnap').disabled = !snapChoice;
}
$('confirmSnap').onclick = async () => {
    if (!snapChoice) {
        return;
    }
    const data = snapChoice;
    cancelTransform();
    await documentAction('snap', data);
};
$('cancelSnap').onclick = cancelTransform;
document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
        cancelTransform();
        return;
    }
    if (
        ['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName) ||
        document.querySelector('dialog[open]')
    ) {
        return;
    }
    if (e.key.toLowerCase() === 'g') {
        setTool('move');
    }
    if (e.key.toLowerCase() === 'r') {
        setTool('rotate');
    }
});

const baseFocusSpatial = focusSelection;
focusSelection = function () {
    const items = selectionItems();
    if (items.length > 1) {
        fitPoints(items.flatMap((e) => (e.path?.length ? e.path : [e.end.position])));
    } else {
        baseFocusSpatial();
    }
};
$('focus').onclick = focusSelection;
