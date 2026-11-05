'use strict';
const $ = (id) => document.getElementById(id);
const token = document.querySelector('meta[name="editor-token"]').content;
let state;
let scene;
let oldScene;
let previewModel;
let selected;
let before = false;
let busy = false;
let drag;
let yaw = 0.7;
let pitch = 0.7;
let center = [0, 0, 0];
let scale = 1;
let hit = [];
let endpoint;
const canvas = $('mesh');
const overlay = $('overlay');
const ctx = overlay.getContext('2d');
const gl = canvas.getContext('webgl');
if (!gl) {
    throw Error('WebGL unavailable');
}
function shader(type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        throw Error(gl.getShaderInfoLog(s));
    }
    return s;
}
function notify(message) {
    $('status').textContent = message;
}
async function api(path, data) {
    const options =
        data === undefined
            ? {}
            : {
                  method: 'POST',
                  headers: { 'Content-Type': 'application/json', 'X-Editor-Token': token },
                  body: JSON.stringify(data),
              };
    const r = await fetch(path, options);
    const d = await r.json();
    if (!r.ok) {
        throw Error(d.error);
    }
    return d;
}
async function work(label, fn) {
    if (busy) {
        return;
    }
    busy = true;
    document.querySelectorAll('button,input,select,textarea').forEach((b) => (b.disabled = true));
    notify(label);
    try {
        await fn();
        notify('');
    } catch (e) {
        notify(e.message);
    } finally {
        busy = false;
        document
            .querySelectorAll('button,input,select,textarea')
            .forEach((b) => (b.disabled = false));
    }
}
function currentModel() {
    return before ? oldScene.model : previewModel || state.model;
}
function currentEntity() {
    return currentModel().entities.find((e) => e.id === selected);
}
function chosenSector() {
    return currentEntity()?.sector || Number($('sector').value) || 1;
}
function fillEntities() {
    const options = state.model.entities.filter(
        (e) => e.editable && ($('category').value === 'all' || e.category === $('category').value)
    );
    $('entities').replaceChildren();
    for (const e of options) {
        $('entities').add(
            new Option(
                `${e.sector ? 'S' + e.sector : '全局'} · ${e.label || e.geometry_kind} · ${e.id.replace(/^route\/id\//, '')}`,
                e.id
            )
        );
    }
    if (!options.some((e) => e.id === selected)) {
        selected = $('entities').value;
    }
    $('entities').value = selected;
}
function populate() {
    $('revision').textContent = state.saved ? '已保存到源文件' : '草稿已自动保存';
    $('workspace').textContent = state.workspace;
    fillEntities();
    if ($('sector').options.length === 1) {
        for (let i = 1; i <= state.model.sectors; i++) {
            $('sector').add(new Option(`第 ${i} 小节`, i));
        }
    }
    $('before').textContent = before ? '返回修改后' : '修改前';
    const rb = state.rebuild;
    $('impact').textContent = state.impact
        ? `受影响实体 ${state.impact.changed_entities.length} 个。重建小节：${rb.rebuilt_sectors.join(', ') || '无（仅更新全局配置）'}；复用：${rb.reused_sectors.join(', ') || '无'}。全局净空、机关、死亡区重新检查。历史通关结果不沿用。`
        : `已构建 ${rb.rebuilt_sectors.length} 个小节。可编辑轨道、地面、连接器和模板实例。`;
    $('checks').replaceChildren();
    for (const [name, r] of Object.entries(state.checks)) {
        const card = document.createElement('div');
        card.className = 'card';
        const title = document.createElement('div');
        title.className = r.status === 'failed' ? 'bad' : r.status === 'passed' ? 'ok' : 'warn';
        title.textContent =
            ({
                'validation.json': '连接与空间',
                'partial-validation.json': '本小节试跑',
                'full-validation.json': '全关试跑',
            }[name] || name) +
            ' · ' +
            ({ passed: '通过', review: '有待确认的提示', failed: '需要调整' }[r.status] ||
                r.status);
        card.append(title);
        const scope = document.createElement('div');
        scope.textContent =
            r.freshness.status !== 'current'
                ? '历史结果：地图已经修改，请重新运行。'
                : r.scope?.frames
                  ? `从第 ${r.scope.sector} 小节运行 ${r.scope.frames} 帧${r.scope.sector > 1 ? '；更早小节未验证' : '；全关范围'}`
                  : '静态检查；尚未证明玩法通过';
        card.append(scope);
        for (const [k, v] of Object.entries(r.checks || {})) {
            const d = document.createElement('details');
            const summary = document.createElement('summary');
            summary.textContent =
                ({
                    build: '地图生成',
                    resources: '游戏资源',
                    geometry: '地面几何',
                    connections: '连接',
                    scenery: '装饰避让',
                    mechanisms: '机关空间',
                    play: '通关',
                    gameplay: '玩法',
                    mechanism_motion: '机关运动',
                }[k] || k) +
                '：' +
                ({ passed: '通过', review: '提示', failed: '需要调整' }[v.status] || v.status);
            const pre = document.createElement('pre');
            pre.textContent = JSON.stringify(v, null, 2);
            d.append(summary, pre);
            card.append(d);
        }
        $('checks').append(card);
    }
    choose(false);
}
function target() {
    return currentEntity()?.edit_targets[$('scope').value];
}
function setScope() {
    const t = target();
    if (!t) {
        return;
    }
    $('piece').value = JSON.stringify(t.piece, null, 2);
    $('source').textContent = t.pointer;
    const scope = $('scope').value;
    $('scopeHint').textContent =
        scope === 'instance'
            ? '仅覆盖这个展开元素。其他重复副本和模板保持独立；覆盖值使用镜像后的局部坐标。'
            : scope === 'call'
              ? '修改此调用的 with、repeat、mirror，影响该调用的所有重复副本。'
              : scope === 'template'
                ? '修改共享默认参数。所有使用默认值的实例都会更新；调用中指定的值优先。'
                : scope === 'placement'
                  ? 'at 为所属元素出口的 [前进、右移、升高]。预览显示经过净空拟合后的完整装饰。'
                  : scope === 'rule'
                    ? '设置依附模式、允许位移、支撑梁和省略权限。预览会重新检查实际装饰网格。'
                    : scope === 'settings'
                      ? '可增删配置条目。接触意图需要理由；死亡区 keepouts 使用世界坐标包围盒。预览执行完整空间检查。'
                      : '编辑资源参数或几何；运行时机关的更改作用于所属路线元素，状态和包络由构建器重新计算。';
    $('reset').hidden = scope !== 'instance' || !currentEntity().overridden;
    if (typeof renderProperties === 'function') {
        renderProperties();
    }
}
function choose(focus = false) {
    const e = currentEntity();
    if (!e) {
        return;
    }
    $('title').textContent =
        `${e.sector ? 'S' + e.sector : '全局'} / ${e.label || e.geometry_kind}`;
    const oldScope = $('scope').value;
    $('scope').replaceChildren();
    for (const [key, t] of Object.entries(e.edit_targets)) {
        $('scope').add(new Option(t.label, key));
    }
    if (e.edit_targets[oldScope]) {
        $('scope').value = oldScope;
    }
    setScope();
    $('details').textContent = JSON.stringify(e.details || {}, null, 2);
    if (focus && !e.global) {
        center = e.end.position.slice();
        scale = e.category === 'geometry' ? 3 : 5;
    }
    $('entities').value = selected;
    draw();
}

function upload() {
    uploadScene();
    draw();
}

function basis() {
    return [
        [Math.cos(yaw), 0, -Math.sin(yaw)],
        [-Math.sin(pitch) * Math.sin(yaw), Math.cos(pitch), -Math.sin(pitch) * Math.cos(yaw)],
        [Math.cos(pitch) * Math.sin(yaw), Math.sin(pitch), Math.cos(pitch) * Math.cos(yaw)],
    ];
}
function dot(a, b) {
    return a.reduce((s, v, i) => s + v * b[i], 0);
}
function project(p) {
    const [r, u] = basis();
    const d = p.map((v, i) => v - center[i]);
    return [
        overlay.clientWidth / 2 + dot(r, d) * scale,
        overlay.clientHeight / 2 - dot(u, d) * scale,
    ];
}
function draw() {
    if (!state || !scene) {
        return;
    }
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    const dpr = devicePixelRatio || 1;
    canvas.width = overlay.width = Math.round(w * dpr);
    canvas.height = overlay.height = Math.round(h * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.enable(gl.DEPTH_TEST);
    gl.clearColor(0.045, 0.065, 0.09, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    renderScene(w, h);
    ctx.clearRect(0, 0, w, h);
    if (typeof drawEditorGrid === 'function') {
        drawEditorGrid();
    }
    hit = [];
    endpoint = null;
    const model = currentModel();
    const sec = Number($('sector').value);
    drawSpaces(model, sec);
    for (const e of model.entities) {
        if (
            !e.editable ||
            e.global ||
            (sec && e.sector !== sec) ||
            ($('category').value !== 'all' && e.category !== $('category').value)
        ) {
            continue;
        }
        const p = project(e.end.position);
        hit.push({ e, p });
        if (typeof selectionHas === 'function' ? !selectionHas(e.id) : e.id !== selected) {
            continue;
        }
        if (e.id === selected) {
            endpoint = p;
        }
        const path = e.path || [];
        if (path.length) {
            ctx.strokeStyle = '#83f4d2';
            ctx.lineWidth = 8;
            ctx.shadowColor = '#83f4d2';
            ctx.shadowBlur = 14;
            ctx.beginPath();
            path.forEach((v, i) => {
                const q = project(v);
                i ? ctx.lineTo(...q) : ctx.moveTo(...q);
            });
            ctx.stroke();
            ctx.shadowBlur = 0;
            const q = project(path[0]);
            ctx.fillStyle = '#133c35';
            ctx.strokeStyle = '#c0ffeb';
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.arc(...q, 5, 0, 7);
            ctx.fill();
            ctx.stroke();
        }
        ctx.fillStyle = '#e0fff3';
        ctx.beginPath();
        ctx.arc(...p, 6, 0, 7);
        ctx.fill();
    }
    if (typeof drawHandles === 'function') {
        drawHandles();
    }
    if (typeof drawEditorOverlay === 'function') {
        drawEditorOverlay();
    }
}
function drawSpaces(model, sector) {
    if ($('space').value === 'none') {
        return;
    }
    const chosen = currentEntity();
    const owner = chosen?.id.replace(/^editor\/rule\//, '');
    const ids = new Set([owner, chosen?.parent]);
    let count = 0;
    for (const v of model.volumes || []) {
        const phase = v.source?.phase || v.state;
        if (
            $('motionPhase').value !== 'all' &&
            phase !== 'always' &&
            phase !== $('motionPhase').value
        ) {
            continue;
        }
        if (sector && v.source?.sector && v.source.sector !== sector) {
            continue;
        }
        if (
            $('space').value === 'selected' &&
            !ids.has(v.owner) &&
            v.source?.parent !== owner &&
            !(owner === 'editor/settings/mechanisms' && v.owner.includes('/mechanism/')) &&
            !(
                owner === 'editor/settings/scenery' &&
                ['scenery assembly', 'balloon departure'].includes(v.rule)
            )
        ) {
            continue;
        }
        const b = v.box;
        const pts = [];
        for (let i = 0; i < 8; i++) {
            pts.push(project([b[i & 1], b[2 + ((i >> 1) & 1)], b[4 + ((i >> 2) & 1)]]));
        }
        ctx.strokeStyle =
            v.rule === 'death zone'
                ? '#d77d8266'
                : v.rule === 'scenery assembly'
                  ? '#eabd7c99'
                  : '#ab9aff55';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (let i = 0; i < 8; i++) {
            for (const bit of [1, 2, 4]) {
                if (!(i & bit)) {
                    ctx.moveTo(...pts[i]);
                    ctx.lineTo(...pts[i | bit]);
                }
            }
        }
        ctx.stroke();
        count++;
    }
    if (count) {
        ctx.fillStyle = '#d7caff';
        ctx.font = '12px system-ui';
        ctx.fillText(`${count} 个空间包络 · ${$('motionPhase').value}`, 22, 130);
    }
}
function fit() {
    const points = currentModel().entities.flatMap((e) => [e.end.position, ...(e.path || [])]);
    const bounds = [
        [Infinity, Infinity, Infinity],
        [-Infinity, -Infinity, -Infinity],
    ];
    for (const p of points) {
        for (let j = 0; j < 3; j++) {
            bounds[0][j] = Math.min(bounds[0][j], p[j]);
            bounds[1][j] = Math.max(bounds[1][j], p[j]);
        }
    }
    center = bounds[0].map((v, i) => (v + bounds[1][i]) / 2);
    scale =
        (Math.min(canvas.clientWidth, canvas.clientHeight) * 0.8) /
        Math.max(...bounds[0].map((v, i) => bounds[1][i] - v));
    draw();
}
async function reload() {
    state = await api('/state');
    previewModel = null;
    before = false;
    oldScene = null;
    scene = await api('/scene');
    populate();
    upload();
}
function payload() {
    return {
        entity: selected,
        piece: JSON.parse($('piece').value),
        scope: $('scope').value,
        revision: state.revision,
    };
}
function requireBuilt() {
    const e = state.model.entities.find((e) => e.id === selected);
    if (
        JSON.stringify(JSON.parse($('piece').value)) !==
        JSON.stringify(e.edit_targets[$('scope').value]?.piece)
    ) {
        throw Error('参数尚未构建，请先重建草稿，再验证或保存');
    }
}
$('scope').onchange = () => {
    previewModel = null;
    setScope();
    draw();
};
$('reset').onclick = () =>
    work('移除实例覆盖并重建…', async () => {
        await api('/apply', {
            entity: selected,
            scope: 'instance',
            reset: true,
            revision: state.revision,
        });
        await reload();
    });
$('piece').oninput = () => {
    previewModel = null;
    notify('参数尚未构建；场景与验证仍对应上次构建。');
    draw();
};
async function preview() {
    const d = await api('/preview', payload());
    previewModel = d.model;
    $('details').textContent = JSON.stringify(currentEntity()?.details || {}, null, 2);
    $('impact').textContent =
        `${d.impact.changed_entities.length} 个实体改变，新增 ${d.impact.added_entities.length}，删除 ${d.impact.removed_entities.length}；候选重建小节 ${d.impact.rebuild_candidates.join(', ') || '无'}。${
            d.spatial_checks
                ? '空间预览检查：' +
                  Object.entries(d.spatial_checks)
                      .map(([k, v]) => k + ' ' + v.status)
                      .join('；')
                : '构建时重算全局约束'
        }。`;
    draw();
}
$('category').onchange = () => {
    previewModel = null;
    fillEntities();
    choose();
    draw();
};
$('space').onchange = draw;
$('motionPhase').onchange = draw;
$('entities').onchange = () => {
    selected = $('entities').value;
    previewModel = null;
    choose(true);
};
$('fit').onclick = fit;
$('focus').onclick = () => choose(true);
$('top').onclick = () => {
    pitch = Math.PI / 2;
    yaw = 0;
    draw();
};
$('iso').onclick = () => {
    pitch = 0.7;
    yaw = 0.7;
    draw();
};
$('sector').onchange = upload;
$('others').onchange = upload;
$('preview').onclick = () => work('解析连接与影响…', preview);
$('apply').onclick = () =>
    work('正在重建受影响小节，并重新检查全局约束…', async () => {
        await api('/apply', payload());
        await reload();
    });
$('save').onclick = () =>
    work('保存源路线…', async () => {
        requireBuilt();
        state = await api('/save', { revision: state.revision });
        populate();
    });
$('refresh').onclick = () => work('刷新构建与证据状态…', reload);
$('before').onclick = () =>
    work('加载前后对比…', async () => {
        if (!state.before_available) {
            throw Error('尚未修改并重建');
        }
        if (!oldScene) {
            oldScene = await api('/before');
        }
        before = !before;
        $('before').textContent = before ? '返回修改后' : '修改前';
        choose();
        upload();
    });
for (const [id, full] of [
    ['verify', false],
    ['full', true],
]) {
    $(id).onclick = () =>
        work('正在运行原版物理与玩法验证；结束后显示报告…', async () => {
            requireBuilt();
            await api('/verify', {
                revision: state.revision,
                sector: full ? 1 : chosenSector(),
                frames: Number($('frames').value),
            });
            state = await api('/state');
            populate();
        });
}
$('launch').onclick = () =>
    work('打开原生游戏现场…', async () => {
        requireBuilt();
        await api('/launch', {
            revision: state.revision,
            sector: chosenSector(),
            handoff: $('handoff').value ? Number($('handoff').value) : null,
        });
    });
overlay.oncontextmenu = (e) => e.preventDefault();
overlay.onwheel = (e) => {
    e.preventDefault();
    scale = Math.max(0.05, Math.min(30, scale * Math.exp(-e.deltaY * 0.001)));
    draw();
};
overlay.onpointerdown = (e) => {
    if (busy) {
        return;
    }
    const p = [e.offsetX, e.offsetY];
    const near = hit
        .map((h) => ({ ...h, d: Math.hypot(h.p[0] - p[0], h.p[1] - p[1]) }))
        .filter((h) => h.d < 12)
        .sort((a, b) => a.d - b.d)[0];
    if (near && near.e.id !== selected) {
        selected = near.e.id;
        choose();
        return;
    }
    drag = {
        x: e.clientX,
        y: e.clientY,
        port:
            !before &&
            ['piece', 'instance', 'placement'].includes($('scope').value) &&
            endpoint &&
            Math.hypot(endpoint[0] - p[0], endpoint[1] - p[1]) < 12,
        pan: e.shiftKey || e.button === 2,
        changed: false,
    };
    overlay.setPointerCapture(e.pointerId);
};
overlay.onpointermove = (e) => {
    if (!drag) {
        return;
    }
    const dx = e.clientX - drag.x;
    const dy = e.clientY - drag.y;
    drag.x = e.clientX;
    drag.y = e.clientY;
    if (drag.port) {
        try {
            const p = JSON.parse($('piece').value);
            const segments = (p.connector || p.rail_path || p.beam_path || p.floor_path)?.segments;
            const last = segments?.at(-1);
            const r = basis()[0];
            const u = basis()[1];
            const a = dx / scale;
            const b = -dy / scale;
            const det = r[0] * u[2] - r[2] * u[0];
            const wx = (a * u[2] - b * r[2]) / det;
            const wz = (b * r[0] - a * u[0]) / det;
            if (Math.abs(det) < 1e-6) {
                return;
            }
            if ($('scope').value === 'placement') {
                const parent = state.model.entities.find((e) => e.id === p.parent);
                if (!parent) {
                    return;
                }
                const [hx, hz] = parent.end.heading;
                p.at[0] += wx * hx + wz * hz;
                p.at[1] += wx * hz - wz * hx;
                $('piece').value = JSON.stringify(p, null, 2);
                drag.changed = true;
                notify('释放鼠标检查装饰位置与空间约束');
                return;
            }
            if (!last || !(last.bezier || last.line)) {
                return;
            }
            const entry = state.model.connections.connections.find(
                (c) => c.to.owner === selected
            )?.to;
            if (!entry) {
                return;
            }
            const [hx, hz] = entry.heading;
            const df = wx * hx + wz * hz;
            const dr = wx * hz - wz * hx;
            const points = last.bezier ? last.bezier.slice(1) : [last.line];
            for (const pt of points) {
                pt[0] += df;
                pt[1] += dr;
            }
            $('piece').value = JSON.stringify(p, null, 2);
            drag.changed = true;
            notify('释放鼠标计算连接影响；重建后显示新网格');
        } catch (err) {
            notify(err.message);
        }
    } else if (drag.pan) {
        const [r, u] = basis();
        center = center.map((v, i) => v - (dx * r[i]) / scale + (dy * u[i]) / scale);
        draw();
    } else {
        yaw -= dx * 0.006;
        pitch = Math.max(0.15, Math.min(1.57, pitch + dy * 0.006));
        draw();
    }
};
overlay.onpointerup = () => {
    const changed = drag?.changed;
    drag = null;
    if (changed) {
        work('计算出口移动后的影响…', preview);
    }
};
new ResizeObserver(draw).observe($('viewport'));
