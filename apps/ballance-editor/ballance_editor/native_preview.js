/* Inspect real native poses without altering the authored document. */
let nativeMotion = null;
let nativeRevision = null;
let nativePlaying = false;
let nativeTick = 0;
let nativeClock = 0;
let nativeSelection = '';
let nativeInstance = '';
let nativeJointAngles = new Map();
let nativePoseRevision = null;
function matrixProduct(a, b) {
    return Array.from({ length: 16 }, (_, i) => {
        const c = Math.floor(i / 4);
        const r = i % 4;
        return [0, 1, 2, 3].reduce((sum, k) => sum + a[k * 4 + r] * b[c * 4 + k], 0);
    });
}
function jointMatrix(joint, angle) {
    const [x, y, z] = joint.axis;
    const a = (angle * Math.PI) / 180;
    const c = Math.cos(a);
    const s = Math.sin(a);
    const t = 1 - c;
    const p = joint.pivot;
    const m = [
        t * x * x + c,
        t * x * y + s * z,
        t * x * z - s * y,
        0,
        t * x * y - s * z,
        t * y * y + c,
        t * y * z + s * x,
        0,
        t * x * z + s * y,
        t * y * z - s * x,
        t * z * z + c,
        0,
        0,
        0,
        0,
        1,
    ];
    for (let i = 0; i < 3; i++) {
        m[12 + i] = p[i] - p.reduce((sum, v, k) => sum + m[k * 4 + i] * v, 0);
    }
    return m;
}
function nativeAvailableTargets() {
    const active = before ? oldScene : scene;
    const ids = new Set(selection.size ? [...selection] : [selected]);
    return (active?.objects || []).filter(
        (o) => o.native && (ids.has(o.parent) || ids.has(o.instance))
    );
}
function nativeTargets() {
    return nativeAvailableTargets().filter((o) => !nativeInstance || o.instance === nativeInstance);
}
function traceSample(track, frame) {
    if (
        !track?.samples?.length ||
        !track.active_ranges.some(([a, b]) => frame >= a && frame <= b)
    ) {
        return null;
    }
    const samples = track.samples;
    let lo = 0;
    let hi = samples.length - 1;
    while (lo < hi) {
        const mid = Math.ceil((lo + hi) / 2);
        if (samples[mid][0] <= frame) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return samples[lo][0] <= frame ? samples[lo][1] : null;
}
function nativePoseMatrix(obj) {
    const initial = obj.matrix || identityMatrix;
    if (!obj.native || !nativeAvailableTargets().some((o) => o.name === obj.name) || before) {
        return initial;
    }
    if ($('nativeMode').value === 'joint' && obj.joint) {
        const angle = Math.max(
            obj.joint.angles[0],
            Math.min(obj.joint.angles[1], nativeJointAngles.get(obj.instance) || 0)
        );
        return matrixProduct(
            obj.instance_matrix,
            matrixProduct(jointMatrix(obj.joint, angle), obj.template_matrix)
        );
    }
    if (
        $('nativeMode').value === 'recorded' &&
        nativeRevision === state.revision &&
        (!nativeInstance || obj.instance === nativeInstance)
    ) {
        return (
            traceSample(nativeMotion?.tracks?.[obj.name], Number($('nativeTime').value)) || initial
        );
    }
    return initial;
}
function nativeObjectMatrix(obj) {
    const pose = nativePoseMatrix(obj);
    const op = typeof transformPreview === 'undefined' ? null : transformPreview;
    if (
        !op ||
        before ||
        !obj.native ||
        !op.ids.some((id) => id === obj.instance || id === obj.parent)
    ) {
        return pose;
    }
    const rotation = jointMatrix({ pivot: op.pivot, axis: [0, 1, 0] }, op.angle || 0);
    for (let i = 0; i < 3; i++) {
        rotation[12 + i] += op.delta[i];
    }
    return matrixProduct(rotation, pose);
}
function stopNative() {
    nativePlaying = false;
    $('nativePlay').textContent = '播放';
}
function syncNativePreview() {
    if (!state) {
        return;
    }
    if (nativePoseRevision !== state.revision) {
        nativeJointAngles.clear();
        nativePoseRevision = state.revision;
    }
    const available = nativeAvailableTargets();
    if (nativeInstance && !available.some((o) => o.instance === nativeInstance)) {
        nativeInstance = '';
    }
    const targets = nativeTargets();
    const signature = [
        state.revision,
        before,
        nativeInstance,
        ...available.map((o) => o.name),
    ].join('|');
    if (signature === nativeSelection) {
        return;
    }
    nativeSelection = signature;
    stopNative();
    $('nativeAngle').value = nativeJointAngles.get(nativeInstance) || 0;
    $('nativeAngleValue').textContent = $('nativeAngle').value + '°';
    if (nativeRevision !== state.revision) {
        nativeMotion = null;
    }
    const selector = $('nativeInstance');
    selector.replaceChildren(new Option('整组选中机关', ''));
    for (const id of new Set(available.map((o) => o.instance))) {
        const item = (before ? oldScene : scene).native_instances.find((i) => i.id === id);
        selector.add(new Option(nativeResourceName(item?.resource || id), id));
    }
    selector.value = nativeInstance;
    nativeInspectorContext();
    $('nativeFocus').disabled = !targets.length;
    $('nativeVerify').disabled =
        busy ||
        before ||
        !!state.document_error ||
        !!state.model.connections.open_connections?.length ||
        !targets.length;
    const tracks = targets
        .map((o) => nativeMotion?.tracks?.[o.name])
        .filter((t) => t?.samples.length);
    const ranges = tracks.flatMap((t) => t.active_ranges);
    const hasJoint = targets.some((o) => o.joint);
    $('nativeMode').querySelector('[value=joint]').disabled = !hasJoint || before;
    $('nativeMode').querySelector('[value=recorded]').disabled = before;
    if ($('nativeMode').value === 'joint' && !hasJoint) {
        $('nativeMode').value = 'initial';
    }
    if (ranges.length) {
        $('nativeTime').min = Math.min(...ranges.map((r) => r[0]));
        $('nativeTime').max = Math.max(...ranges.map((r) => r[1]));
        const moving = tracks.map((t) => t.first_displaced_frame).filter((v) => v !== null);
        $('nativeTime').value = moving.length
            ? Math.max(Number($('nativeTime').min), Math.min(...moving) - 30)
            : $('nativeTime').min;
        $('nativeFrame').textContent = $('nativeTime').value;
    }
    $('nativePlay').disabled = !tracks.length;
    $('nativeTime').disabled = !tracks.length;
    const mode = $('nativeMode').value;
    $('nativeJointRow').hidden = mode !== 'joint';
    $('nativeReplayRow').hidden = mode !== 'recorded';
    $('nativeInfo').textContent = !targets.length
        ? '请选择机关所在的路线段。'
        : mode === 'joint'
          ? `${targets.length} 个真实组件。按原版关节轴调整角度，仅作姿态示意，不模拟接触和驱动。`
          : mode === 'recorded'
            ? tracks.length
                ? `${tracks.length} 个组件有原生记录。未记录组件保持初始姿态；空缺区间不插值。`
                : nativeMotion?.reason || '载入当前地图的原生记录…'
            : `${targets.length} 个真实组件 · 游戏原始网格、材质与装配姿态。`;
}
async function loadNativeMotion() {
    const revision = state.revision;
    const result = await api('/motion-preview');
    if (revision !== state.revision) {
        return;
    }
    nativeMotion = result;
    nativeRevision = revision;
    nativeSelection = '';
    syncNativePreview();
    draw();
}
function nativeResourceName(name) {
    return (
        {
            RotatingArm: '旋转臂',
            Drawbridge: '吊桥',
            SwitchBridge: '翻转桥',
            Sack: '摆锤',
            TiltingSlab: '翻板',
            Fan: '风扇',
            Balloon: '终点气球',
            Lift: '升降机',
            Swing: '摆桥',
            PlateBridge: '活动板桥',
        }[name] || name
    );
}
const drawBeforeNative = draw;
draw = function () {
    syncNativePreview();
    drawBeforeNative();
};
$('nativeToggle').onclick = () => {
    const open = $('nativeControls').hidden;
    $('nativeControls').hidden = !open;
    $('nativeToggle').setAttribute('aria-expanded', String(open));
    nativeSelection = '';
    syncNativePreview();
};
$('nativeMode').onchange = async () => {
    if (typeof clearModelHover === 'function') {
        clearModelHover();
    }
    stopNative();
    nativeSelection = '';
    syncNativePreview();
    draw();
    if ($('nativeMode').value === 'recorded') {
        try {
            await loadNativeMotion();
        } catch (e) {
            $('nativeInfo').textContent = e.message;
        }
    }
};
$('nativeAngle').oninput = () => {
    if (typeof clearModelHover === 'function') {
        clearModelHover();
    }
    for (const o of nativeTargets()) {
        nativeJointAngles.set(o.instance, Number($('nativeAngle').value));
    }
    $('nativeAngleValue').textContent = $('nativeAngle').value + '°';
    draw();
};
$('nativeTime').oninput = () => {
    stopNative();
    $('nativeFrame').textContent = $('nativeTime').value;
    draw();
};
$('nativePlay').onclick = () => {
    if (nativePlaying) {
        stopNative();
        return;
    }
    nativePlaying = true;
    nativeTick = 0;
    nativeClock = Number($('nativeTime').value);
    $('nativePlay').textContent = '暂停';
    requestAnimationFrame(nativeAnimate);
};
function nativeAnimate(time) {
    if (!nativePlaying) {
        return;
    }
    const elapsed = nativeTick ? time - nativeTick : 0;
    nativeTick = time;
    const slider = $('nativeTime');
    const frame = nativeClock + elapsed / (nativeMotion?.frame_ms || 16.667);
    nativeClock = frame;
    slider.value = Math.min(Number(slider.max), frame);
    $('nativeFrame').textContent = Math.round(Number(slider.value));
    draw();
    if (frame >= Number(slider.max)) {
        stopNative();
    }
    if (nativePlaying) {
        requestAnimationFrame(nativeAnimate);
    }
}

$('nativeInstance').onchange = () => {
    if (typeof clearModelHover === 'function') {
        clearModelHover();
    }
    nativeInstance = $('nativeInstance').value;
    const e = state.model.entities.find((e) => e.id === nativeInstance);
    if (e?.category === 'mechanism') {
        selectEntity(e);
        return;
    }
    nativeSelection = '';
    syncNativePreview();
    draw();
};

function nativeInspectorContext() {
    if (selectionItems().length > 1) {
        return;
    }
    if (nativeInstance) {
        const item = (before ? oldScene : scene).native_instances.find(
            (i) => i.id === nativeInstance
        );
        $('title').textContent = nativeResourceName(item?.resource || '机关');
        $('phase').textContent =
            currentEntity()?.category === 'mechanism'
                ? '独立编辑此机关；安装与动力参数独立保存；试玩加载专用原生资源，支撑地面保持原位。'
                : '选择一个机关以独立编辑，或整体变换当前组合。';
    } else if (selectionItems().length === 1) {
        $('title').textContent = entityName(currentEntity());
        $('phase').textContent = state.model.free_layout
            ? '自由布局：其他零件保持原位，接头可用吸附对齐。'
            : '选择位置与尺寸，或直接拖动画布中的手柄。';
    }
}
