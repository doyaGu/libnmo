const test = require('node:test');
const assert = require('node:assert/strict');
const p = require('../ballance_editor/scene_picking.js');
const I = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
function object(name, z) {
    return { name, batches: [{ vertices: [-1, -1, z, 1, -1, z, 0, 1, z] }] };
}
test('Nearest visible surface wins over a mechanism behind a floor', () => {
    const bridge = object('bridge', 0);
    const floor = object('floor', 3);
    assert.equal(p.hit([bridge, floor], [0, 0, 10], [0, 0, -1], () => I).object.name, 'floor');
    assert.equal(p.hit([bridge], [0, 0, 10], [0, 0, -1], () => I).object.name, 'bridge');
    assert.equal(
        p.hit([bridge, floor], [5, 0, 10], [0, 0, -1], () => I),
        null
    );
});
test('Picking follows rotated, translated, nonuniformly scaled native mesh poses', () => {
    const matrix = [0, 0, -2, 0, 0, 3, 0, 0, 4, 0, 0, 0, 20, 30, 40, 1];
    const inv = p.inverse(matrix);
    const source = [0.2, 0.3, 0.4];
    const back = p.point(inv, p.point(matrix, source));
    back.forEach((v, i) => assert.ok(Math.abs(v - source[i]) < 1e-8));
    const obj = object('moving bridge', 0);
    const hit = p.hit([obj], [30, 30, 40], [-1, 0, 0], () => matrix);
    assert.ok(hit);
    assert.ok(Math.abs(hit.distance - 10) < 1e-8);
    assert.deepEqual(hit.position, [20, 30, 40]);
    assert.equal(
        p.hit([obj], [0, 0, 10], [0, 0, -1], () => matrix),
        null
    );
});
test('Edge-on boxes and rays starting inside remain selectable', () => {
    assert.ok(p.boxHit([0, 0, 0], [0, 0, 1], [-1, -1, -1, 1, 1, 1]));
    assert.equal(p.boxHit([2, 0, 0], [0, 0, 1], [-1, -1, -1, 1, 1, 1]), false);
    assert.equal(p.inverse(new Array(16).fill(0)), null);
});
