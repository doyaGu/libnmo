const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const code = fs.readFileSync(__dirname + '/../ballance_editor/native_preview.js', 'utf8');
const c = {};
vm.createContext(c);
vm.runInContext(code.slice(0, code.indexOf('function stopNative')), c);
test('Joint rotation fixes its pivot and rotates offsets about the actual axis', () => {
    const m = c.jointMatrix({ pivot: [2, 3, 4], axis: [0, 1, 0] }, 90);
    function point(p) {
        return [0, 1, 2].map((i) => m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i]);
    }
    assert.deepEqual(point([2, 3, 4]), [2, 3, 4]);
    point([3, 3, 4]).forEach((v, i) => assert.ok(Math.abs(v - [2, 3, 3][i]) < 1e-8));
});
test('Recorded playback never borrows poses across inactive gaps', () => {
    const t = {
        samples: [
            [1, ['first']],
            [3, ['last']],
            [20, ['reset']],
            [22, ['end']],
        ],
        active_ranges: [
            [1, 3],
            [20, 22],
        ],
    };
    assert.equal(c.traceSample(t, 0), null);
    assert.equal(c.traceSample(t, 10), null);
    assert.equal(c.traceSample(t, 23), null);
    assert.equal(c.traceSample(t, 2)[0], 'first');
    assert.equal(c.traceSample(t, 20)[0], 'reset');
});
test('Two mechanisms in one route piece keep independent joint preview angles', () => {
    const I = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    c.before = false;
    c.identityMatrix = I;
    c.selection = new Set(['owner']);
    c.selected = 'owner';
    c.state = { revision: 'r' };
    c.$ = () => ({ value: 'joint' });
    const a = {
        name: 'a/body',
        instance: 'a',
        parent: 'owner',
        native: true,
        matrix: I,
        instance_matrix: I,
        template_matrix: I,
        joint: { pivot: [0, 0, 0], axis: [0, 1, 0], angles: [-180, 180] },
    };
    const b = { ...a, name: 'b/body', instance: 'b' };
    c.scene = { objects: [a, b] };
    vm.runInContext(
        "nativeInstance='a';nativeJointAngles.set('a',90);nativeJointAngles.set('b',0)",
        c
    );
    assert.ok(Math.abs(c.nativeObjectMatrix(a)[0]) < 1e-8);
    assert.deepEqual(Array.from(c.nativeObjectMatrix(b)), I);
    assert.deepEqual(a.matrix, I);
});
test('Dragging a native child previews its complete assembly once without moving its sibling', () => {
    const I = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    c.before = false;
    c.identityMatrix = I;
    c.selection = new Set(['child']);
    c.selected = 'child';
    c.$ = () => ({ value: 'initial' });
    const a = { name: 'child/body', instance: 'child', parent: 'owner', native: true, matrix: I };
    const b = { ...a, name: 'sibling/body', instance: 'sibling' };
    c.scene = { objects: [a, b] };
    c.transformPreview = { ids: ['child'], pivot: [0, 0, 0], delta: [2, 3, 4], angle: 90 };
    assert.deepEqual(Array.from(c.nativeObjectMatrix(a)).slice(12, 15), [2, 3, 4]);
    assert.ok(Math.abs(c.nativeObjectMatrix(a)[0]) < 1e-8);
    assert.deepEqual(Array.from(c.nativeObjectMatrix(b)), I);
    c.transformPreview.ids = ['owner', 'child'];
    assert.deepEqual(Array.from(c.nativeObjectMatrix(a)).slice(12, 15), [2, 3, 4]);
    c.transformPreview = null;
});
