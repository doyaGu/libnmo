const { test } = require('node:test');
const assert = require('node:assert/strict');
const { sketchPiece } = require('../ballance_editor/editor_shapes.js');
test('Sketch visits each clicked point and keeps both native connections aligned', () => {
    const points = [
        [20, 6, 0],
        [35, -4, 0],
        [50, 0, 0],
    ];
    const segments = sketchPiece(points).rail_path.segments;
    assert.deepEqual(
        segments.map((s) => s.bezier[2]),
        points
    );
    assert.deepEqual(segments[0].bezier[0].slice(1), [0, 0]);
    const last = segments.at(-1).bezier;
    assert.deepEqual(last[1].slice(1), last[2].slice(1));
    assert.ok(last[1][0] < last[2][0]);
    for (let i = 1; i < segments.length; i++) {
        const [control, join] = segments[i - 1].bezier.slice(1);
        const next = segments[i].bezier[0];
        for (let axis = 0; axis < 3; axis++) {
            assert.ok(Math.abs(join[axis] - control[axis] - (next[axis] - join[axis])) < 1e-10);
        }
    }
    segments[0].bezier[2][0] = 999;
    assert.equal(points[0][0], 20);
});
test('Empty sketch is a preview without any geometry', () =>
    assert.deepEqual(sketchPiece([]), { rail_path: { segments: [] } }));
