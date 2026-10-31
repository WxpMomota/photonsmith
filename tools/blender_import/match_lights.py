#!/usr/bin/env python3
"""Balance a scene's lights to match a reference picture, usually the render of the same view in another renderer (Eevee and a path tracer spread light differently).

match_lights.py SCENE.pbrt REFERENCE.png [--view filmic|srgb] [--exe ray_tracer] [--width 480] [--spp 32] [--out SCENE_matched.pbrt]

The scene's lights are put in groups (the sky; lights by kind and colour), the scene is rendered once for each group with the others off and once with all of them off, and the gains
that best add up to the reference are solved for: least squares, never negative, weighted so that a dark blue night counts for as much as a bright fire. It prints the gains and
writes the scene with its lights scaled by them. Needs numpy and Pillow. The reference must be from the same camera; `--view` is how it was tone-mapped (filmic: Blender's Filmic).
"""
import math, os, re, shutil, subprocess, sys, tempfile
import numpy as np
from PIL import Image

BOOST = 4.0   # the passes are rendered 4 times as bright (--exposure is a plain multiplier), so that the dark stays above 8-bit rounding; what clips is left out
LIGHT = re.compile(r'^(\s*)(LightSource|AreaLightSource) "(\w+)"(.*)$')


def light_groups(lines):
    """{group name: [line numbers]}: the sky, then lights of a kind and colour; panels and lamps, not the glowing meshes that light nothing or are glow only."""
    groups = {}
    for i, line in enumerate(lines):
        m = LIGHT.match(line)
        if not m or '"bool sampled" false' in line:
            continue
        verb, kind, rest = m.group(2), m.group(3), m.group(4)
        if kind == 'infinite':
            groups.setdefault('sky', []).append(i)
            continue
        colour = re.search(r'"rgb (?:I|L)" \[ ([^\]]+) \]', rest)
        hue = 'x'
        if colour:
            c = [float(x) for x in colour.group(1).split()]
            top = max(max(c), 1e-9)
            hue = '-'.join(f'{round(x / top * 3):d}' for x in c)
        groups.setdefault(f'{kind if verb == "LightSource" else "panel"} {hue}', []).append(i)
    return groups


def scaled(line, k):
    """The light on this line with its power multiplied by k."""
    m = re.search(r'"float scale" (\[ )?([-+0-9.eE]+)( \])?', line)
    if m:
        return line[:m.start(2)] + f'{float(m.group(2)) * k:.6g}' + line[m.end(2):]
    return line.rstrip() + f' "float scale" [ {k:.6g} ]'


def filmic_to_linear(d):
    """A display value of Blender's Filmic back to linear (the grey curve of tone_map.h's filmic_blender, inverted by a table)."""
    stops = np.linspace(-11.0, 7.0, 4000)
    display = -0.011947 + (1.038885 + 0.011947) / (1.0 + np.exp(-0.563328 * (stops - 0.657732))) ** (1.0 / 1.243626)
    return 0.18 * 2.0 ** np.interp(d, np.clip(display, 0.0, 1.0), stops)


def srgb_to_linear(d):
    return np.where(d <= 0.04045, d / 12.92, ((d + 0.055) / 1.055) ** 2.4)


def solve_gains(basis, rest, target, weight, iterations=4000):
    """Non-negative least squares by projected gradient: basis (pixels x groups), rest and target (pixels), weights (pixels)."""
    A = basis * weight[:, None]
    b = (target - rest) * weight
    H = A.T @ A
    g = A.T @ b
    step = 1.0 / max(np.linalg.eigvalsh(H).max(), 1e-12)
    x = np.ones(basis.shape[1])
    y = x.copy()
    t = 1.0
    for _ in range(iterations):   # (accelerated: the groups can be nearly alike)
        nx = np.maximum(0.0, y - step * (H @ y - g))
        nt = 0.5 * (1.0 + math.sqrt(1.0 + 4.0 * t * t))
        y = nx + (t - 1.0) / nt * (nx - x)
        x, t = nx, nt
    return x


def render(exe, scene, out, width, height, spp):
    found = shutil.which(exe) or exe
    done = subprocess.run([os.path.abspath(found), '--gpu', '--denoise', '--tonemap', 'none', '--exposure', str(BOOST), '--output', out, '--height', str(height), str(width), str(spp), '8', scene],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=os.path.dirname(os.path.abspath(found)), text=True, encoding='utf-8', errors='replace')   # (from its folder: its shaders are there)
    if done.returncode != 0 or not os.path.exists(out):
        sys.exit('the render failed:\n' + done.stdout[-1500:])
    return srgb_to_linear(np.asarray(Image.open(out).convert('RGB')).astype(np.float64) / 255.0) / BOOST


def main(argv):
    if len(argv) < 2 or argv[0].startswith('-'):
        print(__doc__)
        return 2
    scene, reference = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    def option(name, default):
        return argv[argv.index(name) + 1] if name in argv else default
    view, exe = option('--view', 'filmic'), option('--exe', 'ray_tracer')
    width, spp = int(option('--width', '480')), int(option('--spp', '32'))
    out_scene = option('--out', os.path.splitext(scene)[0] + '_matched.pbrt')
    lines = open(scene, encoding='utf-8').read().split('\n')
    groups = light_groups(lines)
    if not groups:
        print('no lights found')
        return 1
    ref = Image.open(reference).convert('RGB')
    height = max(2, round(width * ref.height / ref.width))
    d = np.asarray(ref.resize((width, height), Image.LANCZOS)).astype(np.float64) / 255.0
    target = filmic_to_linear(d) if view == 'filmic' else srgb_to_linear(d)
    tmp = tempfile.mkdtemp(prefix='match_lights_')
    trial = os.path.join(os.path.dirname(scene), '_match_lights_trial.pbrt')   # (beside the scene: its relative paths still work)
    names = list(groups)
    images = {}
    try:
        for name in names + [None]:
            keep = set(groups[name]) if name else set()
            all_lights = {i for v in groups.values() for i in v}
            text = [scaled(l, 0.0) if i in all_lights and i not in keep else l for i, l in enumerate(lines)]
            open(trial, 'w', encoding='utf-8').write('\n'.join(text))
            print('rendering', name or 'with every light off', flush=True)
            images[name] = render(exe, trial, os.path.join(tmp, 'g.png'), width, height, spp)
    finally:
        if os.path.exists(trial):
            os.remove(trial)
        shutil.rmtree(tmp, ignore_errors=True)
    rest = images[None].reshape(-1)
    basis = np.stack([np.clip(images[n] - images[None], 0.0, None).reshape(-1) for n in names], axis=1)
    want = target.reshape(-1)
    clipped = np.any([img.reshape(-1) >= 0.97 / BOOST for img in images.values()], axis=0)   # (a pass that clipped says nothing about how bright it was)
    use = (want > 1e-4) & (want < 3.0) & ~clipped & (rest < 0.3)   # (nor does what shines by itself say anything about the lights)
    weight = 1.0 / (0.03 + want)                                # (an error measured relative to the light there)
    gains = solve_gains(basis[use], rest[use], want[use], weight[use])
    before = np.sqrt(np.mean(((basis[use].sum(1) + rest[use] - want[use]) * weight[use]) ** 2))
    after = np.sqrt(np.mean(((basis[use] @ gains + rest[use] - want[use]) * weight[use]) ** 2))
    print(f'relative error {before:.3f} -> {after:.3f}')
    for n, g in zip(names, gains):
        print(f'  {n:24} x {g:.3f}  ({len(groups[n])} light{"s" if len(groups[n]) != 1 else ""})')
    for n, g in zip(names, gains):
        for i in groups[n]:
            lines[i] = scaled(lines[i], float(g))
    open(out_scene, 'w', encoding='utf-8').write('\n'.join(lines))
    print('wrote', out_scene)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
