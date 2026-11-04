"""Bounded analytic route families; no expression evaluation or dependencies."""
import math

from .path import number


def curve(kind, spec):
    allowed = {
        'wave': {'length', 'amplitude', 'cycles', 'rise', 'crest'},
        'weave': {'length', 'width', 'lift', 'rise', 'hand'},
        'eased_turn': {'length', 'angle', 'rise'},
    }[kind]
    if not isinstance(spec, dict) or set(spec) - allowed:
        raise ValueError(f'unknown {kind} formula options')
    values = {k: number(v, k) for k, v in spec.items()}
    length = values.get('length', 100)
    rise = values.get('rise', 0)
    if not 5 <= length <= 2000 or abs(rise) > 500:
        raise ValueError('formula length must be 5..2000 and rise within +/-500')
    smooth = lambda u: u*u*(3-2*u)
    if kind == 'wave':
        amplitude, cycles, crest = values.get('amplitude', 12), values.get('cycles', 1), values.get('crest', 0)
        if abs(amplitude) > 200 or not 1 <= cycles <= 8 or cycles != int(cycles) or abs(crest) > 100:
            raise ValueError('wave needs integer cycles 1..8, amplitude +/-200 and crest +/-100')
        def evaluate(u):
            t = 2 * math.pi * cycles * u
            return (length*u, amplitude*math.sin(t)**3,
                    rise*smooth(u) + crest*math.sin(math.pi*u)**2)
        return evaluate
    if kind == 'weave':
        width, lift, hand = values.get('width', 45), values.get('lift', 14), values.get('hand', 1)
        if not 10 <= width <= 500 or abs(lift) > 100 or hand not in (-1, 1):
            raise ValueError('weave needs width 10..500, lift +/-100, hand +/-1')
        a, b = length/2, hand*width/2
        norm = math.hypot(a, 2*b)
        c, s = a/norm, 2*b/norm
        def evaluate(u):
            t = 2 * math.pi * u
            x, y = a*math.sin(t), b*math.sin(2*t)
            return (x*c+y*s, -x*s+y*c, lift*(1-math.cos(t)) + rise*smooth(u))
        return evaluate
    angle = math.radians(values.get('angle', 90))
    if not 0 < abs(angle) <= 2*math.pi:
        raise ValueError('eased_turn angle must be nonzero and within +/-360')
    # Preintegrate theta(u)=angle*(3u²-2u³). Its curvature starts and ends
    # at zero. Simpson integration is deterministic; interpolate only the
    # short residual interval when the sampler requests an arbitrary u.
    count = 4096
    xs, ys = [0.0], [0.0]
    def interval(a, b):
        angles = [angle*smooth(u) for u in (a, (a+b)/2, b)]
        return tuple(length*(b-a)/6 * (fn(angles[0])+4*fn(angles[1])+fn(angles[2]))
                     for fn in (math.cos, math.sin))
    for i in range(count):
        x, y = interval(i/count, (i+1)/count)
        xs.append(xs[-1]+x); ys.append(ys[-1]+y)
    def evaluate(u):
        i = min(int(u*count), count)
        x, y = interval(i/count, u)
        return xs[i]+x, ys[i]+y, rise*smooth(u)
    return evaluate


def sampled(kind, spec, step, tolerance):
    evaluate = curve(kind, spec)
    points = [(0.0, evaluate(0))]
    def split(a, pa, b, pb, depth=0):
        m = (a+b)/2; pm = evaluate(m)
        chord_mid = tuple((p+q)/2 for p, q in zip(pa,pb))
        if math.dist(pa,pb) <= step and math.dist(pm,chord_mid) <= tolerance:
            points.append((b,pb)); return
        if depth >= 20 or len(points) > 19900:
            raise ValueError('formula exceeds the sampling budget')
        split(a,pa,m,pm,depth+1); split(m,pm,b,pb,depth+1)
    # Seed at least sixteen samples per permitted period. This prevents a
    # periodic curve's midpoint from aliasing to its endpoints.
    for i in range(128):
        split(i/128,evaluate(i/128),(i+1)/128,evaluate((i+1)/128))
    result=[]
    for u,p in points:
        lo, hi = max(0,u-1e-6), min(1,u+1e-6)
        a,b = evaluate(lo),evaluate(hi)
        dx,dy=b[0]-a[0],b[1]-a[1]
        norm=math.hypot(dx,dy)
        if norm < 1e-12:
            raise ValueError('formula has a stationary horizontal tangent')
        result.append((p,(dx/norm,dy/norm)))
    return result
