/* Cubic sketch interpolation with aligned route entry and exit tangents. */
function sketchPiece(points) {
    const all = [[0, 0, 0], ...points];
    const segments = [];
    for (let i = 1; i < all.length; i++) {
        const a = all[i - 1];
        const b = all[i];
        const length = Math.hypot(...b.map((v, j) => v - a[j]));
        const previous = all[Math.max(0, i - 2)];
        const next = all[Math.min(all.length - 1, i + 1)];
        const c1 =
            i === 1
                ? [a[0] + length / 3, a[1], a[2]]
                : a.map((v, j) => v + (b[j] - previous[j]) / 6);
        const c2 =
            i === all.length - 1
                ? [b[0] - length / 3, b[1], b[2]]
                : b.map((v, j) => v - (next[j] - a[j]) / 6);
        segments.push({ bezier: [c1, c2, b.slice()] });
    }
    return { rail_path: { segments } };
}
if (typeof module !== 'undefined') {
    module.exports = { sketchPiece };
}
