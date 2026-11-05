/* Direct selection and inspection of rendered native assemblies. */
let hoveredModel = null;
let hoverTime = 0;
function visibleSceneObjects() {
    return [...new Set(renderBatches.map((b) => b.object))].filter(
        (o) => typeof localObjectVisible !== 'function' || localObjectVisible(o)
    );
}
function sceneRay(x, y) {
    const [right, up, depth] = basis();
    const px = (x - overlay.clientWidth / 2) / scale;
    const py = (overlay.clientHeight / 2 - y) / scale;
    return {
        origin: center.map((v, i) => v + px * right[i] + py * up[i] + 10000 * depth[i]),
        direction: depth.map((v) => -v),
    };
}
function pickScene(x, y) {
    const ray = sceneRay(x, y);
    return ScenePicking.hit(visibleSceneObjects(), ray.origin, ray.direction, nativeObjectMatrix);
}
function modelEntity(object) {
    const entities = currentModel().entities;
    return (
        entities.find((e) => e.id === object.instance) ||
        entities.find((e) => e.id === object.parent)
    );
}
function modelLabel(object) {
    const instance = (before ? oldScene : scene).native_instances.find(
        (i) => i.id === object.instance
    );
    const name = nativeResourceName(instance?.resource || '机关');
    const body = object.body || '';
    const part = /Hinge/.test(body)
        ? '铰链'
        : /Bridge/.test(body)
          ? '桥板'
          : /Rotor/.test(body)
            ? '转子'
            : /Rope/.test(body)
              ? '绳索'
              : /Sack/.test(body)
                ? '沙袋'
                : '';
    return name + (part ? ' · ' + part : '');
}
const pickBeforeMeshes = pick;
pick = function (x, y) {
    const result = pickScene(x, y);
    if (result?.object.native) {
        const entity = modelEntity(result.object);
        if (entity?.editable) {
            return entity;
        }
    }
    const entity = pickBeforeMeshes(x, y);
    return result && entity?.edit_owner ? null : entity;
};
const selectBeforeMeshes = selectEntity;
selectEntity = function (entity, focus = false, add = false) {
    if (busy) {
        return;
    }
    nativeInstance = entity?.category === 'mechanism' ? entity.id : '';
    nativeSelection = '';
    selectBeforeMeshes(entity, focus, add);
    if (nativeInstance) {
        $('nativeControls').hidden = false;
        $('nativeToggle').setAttribute('aria-expanded', 'true');
        syncNativePreview();
        draw();
    }
};
function sceneSelectionStrength(obj) {
    if (!obj.native) {
        return 0;
    }
    if (hoveredModel?.name === obj.name) {
        return 0.28;
    }
    return nativeInstance === obj.instance && nativeTargets().some((o) => o.name === obj.name)
        ? 0.18
        : 0;
}
function focusNativeModel() {
    hoveredModel = null;
    $('modelHover').hidden = true;
    const points = [];
    for (const object of nativeTargets()) {
        const b = object.pickBounds || ScenePicking.bounds(object.batches);
        const m = nativeObjectMatrix(object);
        for (let i = 0; i < 8; i++) {
            points.push(
                ScenePicking.point(m, [b[i & 1 ? 3 : 0], b[i & 2 ? 4 : 1], b[i & 4 ? 5 : 2]])
            );
        }
    }
    if (points.length) {
        fitPoints(points);
    }
}
const focusBeforeMeshes = focusSelection;
focusSelection = function () {
    if (nativeInstance && nativeTargets().length) {
        focusNativeModel();
    } else {
        focusBeforeMeshes();
    }
};
$('focus').onclick = focusSelection;
$('nativeFocus').onclick = focusNativeModel;
const moveBeforeMeshes = overlay.onpointermove;
overlay.onpointermove = (event) => {
    moveBeforeMeshes(event);
    if (
        busy ||
        sketch ||
        viewDrag ||
        handleDrag ||
        transformDrag ||
        boxDrag ||
        !['select', 'move', 'rotate'].includes(tool)
    ) {
        clearModelHover();
        return;
    }
    const now = performance.now();
    if (now - hoverTime < 50) {
        return;
    }
    hoverTime = now;
    const result = pickScene(event.offsetX, event.offsetY);
    const object = result?.object.native ? result.object : null;
    const changed = hoveredModel?.name !== object?.name;
    hoveredModel = object;
    const tip = $('modelHover');
    tip.hidden = !object;
    if (object) {
        tip.textContent = modelLabel(object) + ' · 点击选择，双击聚焦';
        tip.style.left = Math.min(event.offsetX + 16, overlay.clientWidth - 240) + 'px';
        tip.style.top = Math.max(130, event.offsetY - 36) + 'px';
    }
    if (changed) {
        draw();
    }
};
function clearModelHover() {
    const had = hoveredModel;
    hoveredModel = null;
    $('modelHover').hidden = true;
    if (had) {
        draw();
    }
}
overlay.addEventListener('pointerleave', clearModelHover);
$('nativeVerify').onclick = async () => {
    if (busy || before) {
        return;
    }
    const sector = nativeTargets()[0]?.sector;
    if (!sector) {
        return;
    }
    stopNative();
    $('nativeVerify').disabled = true;
    await work('正在试跑所选机关的小节…', async () => {
        await api('/verify', {
            revision: state.revision,
            sector,
            frames: Number($('frames').value),
        });
        state = await api('/state');
        populate();
        const check =
            state.checks[sector === 1 ? 'full-validation.json' : 'partial-validation.json']?.checks
                ?.play;
        if (check?.status !== 'passed') {
            throw Error(
                check?.error || check?.reason || '本小节尚未通过，请打开“检查地图”查看原因。'
            );
        }
        $('nativeMode').value = 'recorded';
        await loadNativeMotion();
    });
    nativeSelection = '';
    syncNativePreview();
    draw();
};

const renderBeforeNativeContext = renderTransformControls;
renderTransformControls = function () {
    renderBeforeNativeContext();
    nativeInspectorContext();
};
