"""lamp_and_ball.py -- "Lamp and Ball": a 20 second cartoon rendered by Photonsmith, in the spirit of a desk-lamp-meets-ball short.

A small yellow desk lamp wakes up on a dark desk, notices a red ball rolling in, hops over, nudges it, watches it bounce off a stack of books and roll home, and puts its
shade over it like a blanket. Goodnight.

Every frame is a pbrt-v4 scene written by `build(t)` and rendered by ray_tracer.exe on the GPU (path traced, denoised); ffmpeg joins the frames.

    python scripts/anim/lamp_and_ball.py --still 6.0      # one frame, to look at
    python scripts/anim/lamp_and_ball.py --preview        # a quick low-resolution render of everything
    python scripts/anim/lamp_and_ball.py --final          # the real thing

The story is written in "story space": +x to the right of the picture, +y up, +z towards the camera. pbrt is left-handed, so every emitted x is negated (`W`) and triangle
windings flipped; nothing else in this file has to think about it.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from anim_lib import (Track, lathe, uv_sphere, rounded_box, box, translate, scale, rotate, mat_mul, xform_point, vadd, vsub, vmul, vnorm, vlen, vdot, vcross,
                      attr, coated, diffuse, fmt3, film_cli, clamp01, lerp)

DURATION = 20.0
FPS = 24
W = scale(-1.0, 1.0, 1.0)            # story space -> pbrt world


def emit(mesh, m):
    """A mesh through story-space matrix `m`, as pbrt text in the (mirrored) world."""
    return mesh.to_pbrt(mat_mul(W, m), flip=True)


def wp(p):
    """A story-space point as a pbrt-world point."""
    return (-p[0], p[1], p[2])


# --------------------------------------------------------------------------------------------------------------------------------------------------------------
# The story's cast and numbers
# --------------------------------------------------------------------------------------------------------------------------------------------------------------
BALL_R = 0.55
GRAVITY = 18.0
BASE_R, BASE_H = 0.80, 0.17
L1, L2 = 1.55, 1.45
SHADE_LEN, SHADE_R0, SHADE_R1 = 1.15, 0.30, 0.66
BOOK_X = 4.9                       # the bumper: the books' near face (the ball bounces off it)

# lamp hops: (start time, duration, from x, to x, height)
HOPS = [(6.25, 0.55, -3.4, -2.5, 0.75), (7.05, 0.55, -2.5, -1.6, 0.75), (7.85, 0.55, -1.6, -0.7, 0.75),
        (10.2, 0.60, -0.7, -1.6, 0.9), (11.1, 0.55, -1.6, -2.4, 0.7)]
NUDGE_T = 9.0                      # the head touches the ball
COVER_T = 15.6                     # the shade comes down over the ball


def lamp_x(t):
    x = HOPS[0][2]
    for (t0, dur, xa, xb, h) in HOPS:
        if t >= t0 + dur:
            x = xb
        elif t >= t0:
            x = lerp(xa, xb, ((t - t0) / dur))
            break
    return x


def lamp_hop(t):
    """(height above the desk, vertical scale, width scale) of the lamp's whole body from the hops: anticipation squash, stretch in the air, landing squash and a wobble."""
    for (t0, dur, xa, xb, h) in HOPS:
        pre, post = 0.22, 0.45
        if t0 - pre <= t < t0:                      # crouch
            u = (t - (t0 - pre)) / pre
            s = 1.0 - 0.20 * math.sin(u * math.pi * 0.5)
            return 0.0, s, 1.0 / math.sqrt(s)
        if t0 <= t < t0 + dur:                      # in the air
            u = (t - t0) / dur
            y = 4.0 * h * u * (1.0 - u)
            s = 1.0 + 0.12 * math.sin(u * math.pi)
            return y, s, 1.0 / math.sqrt(s)
        if t0 + dur <= t < t0 + dur + post:        # landing squash settling with a wobble
            u = (t - (t0 + dur)) / post
            s = 1.0 - 0.18 * math.exp(-5.0 * u) * math.cos(u * 3.0 * math.pi)
            return 0.0, s, 1.0 / math.sqrt(s)
    return 0.0, 1.0, 1.0


# --------------------------------------------------------------------------------------------------------------------------------------------------------------
# The ball: a small physics pass, sampled at every frame
# --------------------------------------------------------------------------------------------------------------------------------------------------------------
def simulate_ball(e_wall=0.8, nudge=(3.4, 8.6, 0.0)):
    dt = 1.0 / 480.0
    steps = int(DURATION / dt) + 2
    pos = [9.5, BALL_R, 0.35]
    vel = [0.0, 0.0, 0.0]
    spin = 0.0
    nudged = False
    out = []
    for k in range(steps):
        t = k * dt
        if t < 6.4:                                  # rolls in from the right, slowing to a stop (scripted so that it stops exactly where the story wants it)
            u = clamp01((t - 4.0) / 2.4)
            e = 1.0 - (1.0 - u) ** 2.2
            x_new = lerp(9.5, 2.9, e)
            vel = [(x_new - pos[0]) / dt, 0.0, 0.0]
            pos[0] = x_new
            pos[1] = BALL_R
            pos[2] = lerp(0.35, 0.05, e)
            spin += (vel[0] / BALL_R) * dt * 180.0 / math.pi
            out.append((t, tuple(pos), tuple(vel), spin))
            continue
        if t < NUDGE_T + 0.04:                       # waits
            out.append((t, tuple(pos), (0.0, 0.0, 0.0), spin))
            continue
        if not nudged:                               # the nudge: up and away to the right
            vel = [nudge[0], nudge[1], nudge[2]]
            nudged = True
        vel[1] -= GRAVITY * dt
        pos[0] += vel[0] * dt
        pos[1] += vel[1] * dt
        pos[2] += vel[2] * dt
        if pos[0] > BOOK_X - BALL_R and vel[0] > 0:  # the books: bounce back, a little faster than it came
            pos[0] = BOOK_X - BALL_R
            vel[0] = -vel[0] * e_wall
        if pos[1] < BALL_R:
            pos[1] = BALL_R
            if vel[1] < -1.0:
                vel[1] = -vel[1] * 0.70
                vel[0] *= 0.94
                vel[2] *= 0.8
            else:
                vel[1] = 0.0
                f = 1.0 - 0.9 * dt
                vel[0] *= f
                vel[2] *= f
        if t > 12.2 and pos[1] <= BALL_R + 1e-6:     # rolling home: a stronger drag so that it stops at the lamp
            f = 1.0 - 1.3 * dt
            vel[0] *= f
            vel[2] *= f
        spin += (vel[0] / BALL_R) * dt * 180.0 / math.pi
        out.append((t, tuple(pos), tuple(vel), spin))
    return out


REST_X = -0.3


def _tuned_ball():
    """The ball must come to rest at REST_X, next to the lamp: bisect the books' bounce so that it does."""
    lo, hi = 0.2, 3.0
    best = simulate_ball(1.0)
    for _ in range(40):
        mid = 0.5 * (lo + hi)
        best = simulate_ball(mid)
        if best[-1][1][0] > REST_X:
            lo = mid
        else:
            hi = mid
    return best


_BALL = _tuned_ball()


def ball_state(t):
    return _BALL[min(max(int(t * 480.0), 0), len(_BALL) - 1)]


def ball_rest():
    """Where the ball sits when the lamp covers it: its position at COVER_T - 0.6."""
    return ball_state(COVER_T - 0.6)[1]


# --------------------------------------------------------------------------------------------------------------------------------------------------------------
# The lamp: a base, two arms (solved by inverse kinematics to put the head where the story wants it), a head joint and a shade
# --------------------------------------------------------------------------------------------------------------------------------------------------------------
def arm_ik(j1, j3_target):
    """Elbow position for a two-link arm (lengths L1, L2) in the x-y plane from j1 to as near j3_target as it can reach; elbow up."""
    dx, dy = j3_target[0] - j1[0], j3_target[1] - j1[1]
    dist = max(1e-4, math.hypot(dx, dy))
    dist = min(dist, (L1 + L2) * 0.999)
    ang = math.atan2(dy, dx)
    cos_a = clamp01((L1 * L1 + dist * dist - L2 * L2) / (2.0 * L1 * dist)) if False else max(-1.0, min(1.0, (L1 * L1 + dist * dist - L2 * L2) / (2.0 * L1 * dist)))
    a = math.acos(cos_a)
    e = ang + a                                      # the elbow is above the line from the base to the head
    j2 = (j1[0] + L1 * math.cos(e), j1[1] + L1 * math.sin(e))
    j3 = (j1[0] + dist * math.cos(ang), j1[1] + dist * math.sin(ang))
    return j2, j3


def build_parts():
    parts = {}
    prof = [(0.0, 0.0), (BASE_R - 0.12, 0.0)]
    for i in range(1, 7):
        a = math.pi * 0.5 * i / 6.0
        prof.append((BASE_R - 0.12 + 0.12 * math.sin(a), BASE_H * 0.5 * (1.0 - math.cos(a))))
    prof += [(BASE_R * 0.55, BASE_H), (0.0, BASE_H + 0.02)]
    parts["base"] = lathe(prof, 64)
    parts["sphere"] = uv_sphere(1.0, 32, 16)
    parts["arm"] = lathe([(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)], 20)
    outer = [(0.0, -0.20), (0.16, -0.18), (SHADE_R0 * 0.9, -0.05), (SHADE_R0, 0.0)]
    n = 14
    for i in range(1, n + 1):
        u = i / n
        outer.append((SHADE_R0 + (SHADE_R1 - SHADE_R0) * (math.sin(u * math.pi * 0.5) ** 1.3), SHADE_LEN * u))
    parts["shade_out"] = lathe(outer, 72)
    parts["shade_in"] = lathe(list(reversed([(r - 0.035 if r > 0.2 else r, y) for (r, y) in outer[3:]])), 72)
    parts["rim"] = lathe([(SHADE_R1 - 0.035, SHADE_LEN), (SHADE_R1, SHADE_LEN)], 72)
    parts["ball"] = uv_sphere(1.0, 64, 32)
    return parts


PARTS = build_parts()


def align_y(origin, ydir, hint=(0.0, 0.0, 1.0)):
    """A frame at `origin` whose local +Y is `ydir`."""
    ya = vnorm(ydir)
    xa = vcross(hint, ya)
    if vlen(xa) < 1e-6:
        xa = vcross((1.0, 0.0, 0.0), ya)
    xa = vnorm(xa)
    za = vcross(xa, ya)
    return [xa[0], ya[0], za[0], origin[0], xa[1], ya[1], za[1], origin[1], xa[2], ya[2], za[2], origin[2], 0, 0, 0, 1]


def lamp_head_plan(t, base_x, hop_y):
    """Where the lamp's head joint is and which way the shade points at time t: (j3 target in lamp space, shade direction, glow). Lamp space: origin on the desk below the base, +x forward."""
    rx, ry = ball_state(t)[1][0], ball_state(t)[1][1]
    bx, by = rx - base_x, ry                                       # the ball in lamp space
    rest = ball_rest()
    # --- sleeping: head hangs down in front of the lamp
    sleep_j3 = (1.55, 1.25)
    sleep_dir = vnorm((0.35, -1.0, 0.0))
    awake_j3 = (1.85, 2.85)
    awake_dir = vnorm((1.0, -0.12, 0.0))
    # wake up (2.3 - 3.5): lift with overshoot
    wake = Track([(2.0, 0.0), (3.4, 1.0, "back")])(t)
    j3 = (lerp(sleep_j3[0], awake_j3[0], wake), lerp(sleep_j3[1], awake_j3[1], wake))
    d = vnorm(tuple(lerp(sleep_dir[i], awake_dir[i], wake) for i in range(3)))
    # looks around (3.6 - 5.0): a slow sweep of the head
    look = Track([(3.5, 0.0), (3.95, 1.0), (4.5, -1.0), (5.0, 0.0)])(t)
    d = vnorm((d[0], d[1], d[2] + 0.45 * look * (1.0 if t < 5.4 else 0.0)))
    # tracks the ball from the moment it shows (4.6)
    track_w = Track([(4.6, 0.0), (5.3, 1.0), (9.0, 1.0), (9.3, 0.6), (10.0, 1.0), (15.0, 1.0), (15.3, 0.0)])(t)
    aim = vnorm((bx - j3[0], by - j3[1], ball_state(t)[1][2]))
    if t > 4.6:
        d = vnorm(tuple(lerp(d[i], aim[i], track_w) for i in range(3)))
    # --- the double take (6.0 - 6.25): the head jerks back and up
    take = Track([(5.95, 0.0), (6.1, 1.0, "out"), (6.3, 0.0, "smooth")])(t)
    j3 = (j3[0] - 0.25 * take, j3[1] + 0.35 * take)
    # --- wind-up and nudge (8.3 - 9.05)
    wind = Track([(8.3, 0.0), (8.85, 1.0, "smooth")])(t)
    lunge = Track([(8.85, 0.0), (NUDGE_T, 1.0, "in"), (9.35, 0.0, "out")])(t)
    if 8.3 <= t < 9.4:
        back_j3 = (j3[0] - 0.9, j3[1] + 0.6)
        nud = (bx - 0.45 * d[0] * SHADE_LEN, by + 0.35 - d[1] * SHADE_LEN)
        j3 = (lerp(j3[0], back_j3[0], wind * (1.0 - lunge)), lerp(j3[1], back_j3[1], wind * (1.0 - lunge)))
        if lunge > 0.0:
            j3 = (lerp(j3[0], nud[0], lunge), lerp(j3[1], nud[1], lunge))
            d = vnorm(tuple(lerp(d[i], vnorm((bx - j3[0], by - j3[1], 0.0))[i], lunge) for i in range(3)))
    # --- the cover (14.6 - 15.9): lowers the shade straight down over the ball
    cov = Track([(14.5, 0.0), (COVER_T, 1.0, "smooth")])(t)
    if cov > 0.0:
        over_j3 = (rest[0] - base_x, 0.28 + SHADE_LEN)
        over_dir = (0.0, -1.0, 0.0)
        j3 = (lerp(j3[0], over_j3[0], cov), lerp(j3[1], over_j3[1], cov))
        d = vnorm(tuple(lerp(d[i], over_dir[i], cov) for i in range(3)))
    # --- glow: flickers on at 2.3, pulses with the nudge, dims under the cover
    glow = Track([(0.0, 0.0), (2.25, 0.0), (2.3, 1.0, "linear"), (2.42, 0.0, "linear"), (2.55, 1.0, "linear"), (2.62, 0.2, "linear"), (2.8, 1.0, "linear")])(t)
    glow *= 1.0 + 0.5 * Track([(8.95, 0.0), (9.05, 1.0), (9.4, 0.0)])(t)
    glow *= 1.0 - 0.62 * Track([(16.2, 0.0), (19.0, 1.0)])(t)
    return j3, d, glow


def build_lamp(t):
    base_x = lamp_x(t)
    hop_y, sy, sw = lamp_hop(t)
    world = mat_mul(translate(base_x, hop_y, 0.0), scale(sw, sy, sw))
    j1 = (0.0, BASE_H + 0.1)
    j3_target, d, glow = lamp_head_plan(t, base_x, hop_y)
    j2_2d, j3_2d = arm_ik(j1, j3_target)
    j2 = (j2_2d[0], j2_2d[1], 0.0)
    j3 = (j3_2d[0], j3_2d[1], 0.0)
    j1_3 = (j1[0], j1[1], 0.0)
    body_mat = coated((0.97, 0.72, 0.08), 0.06)
    dark_mat = coated((0.28, 0.28, 0.33), 0.25)
    inner_mat = diffuse((0.93, 0.91, 0.86))
    shapes = [attr(dark_mat, emit(PARTS["base"], world))]
    for jp, r in ((j1_3, 0.16), (j2, 0.14), (j3, 0.14)):
        shapes.append(attr(body_mat, emit(PARTS["sphere"], mat_mul(world, translate(jp), scale(r)))))
    for a, b, r in ((j1_3, j2, 0.07), (j2, j3, 0.065)):
        v = vsub(b, a)
        shapes.append(attr(body_mat, emit(PARTS["arm"], mat_mul(world, align_y(a, v), scale(r, vlen(v), r)))))
    head = mat_mul(world, align_y(j3, d))
    shapes.append(attr(body_mat, emit(PARTS["shade_out"], head)))
    shapes.append(attr(inner_mat, [emit(PARTS["shade_in"], head), emit(PARTS["rim"], head)]))
    bulb_local = (0.0, 0.40, 0.0)
    bulb_c = xform_point(head, bulb_local)
    if glow > 0.01:
        shapes.append(attr('Material "diffuse" "rgb reflectance" [ 1 1 1 ]', emit(PARTS["sphere"], mat_mul(head, translate(bulb_local), scale(0.19))),
                           emission=(30.0 * glow, 25.0 * glow, 17.0 * glow)))
    # a spot light from the bulb along the shade's axis does the real lighting; its cone just fills the shade's mouth
    mouth = xform_point(head, (0.0, SHADE_LEN, 0.0))
    axis = vnorm(vsub(mouth, bulb_c))
    spot = ""
    if glow > 0.01:
        spot = 'LightSource "spot" "point3 from" [ %s ] "point3 to" [ %s ] "float coneangle" [ 36 ] "float conedeltaangle" [ 14 ] "rgb I" [ %s ]' % (
            fmt3(wp(bulb_c)), fmt3(wp(vadd(bulb_c, axis))), fmt3((900.0 * glow, 740.0 * glow, 480.0 * glow)))
    return "\n".join(shapes), spot


# --------------------------------------------------------------------------------------------------------------------------------------------------------------
# The set, the ball, the camera
# --------------------------------------------------------------------------------------------------------------------------------------------------------------
def build_set():
    out = [attr(coated((0.42, 0.22, 0.12), 0.10), emit(box(34.0, 1.0, 18.0), translate(0.0, -0.5, -2.5))),
           attr(diffuse((0.16, 0.22, 0.36)), emit(box(44.0, 22.0, 0.5), translate(0.0, 9.0, -8.0)))]
    y = 0.0
    for (sx, sy, sz, ry, col) in ((2.1, 0.40, 1.9, 8, (0.78, 0.15, 0.18)), (1.9, 0.34, 1.75, -9, (0.15, 0.46, 0.72)), (1.8, 0.30, 1.6, 14, (0.92, 0.77, 0.22)),
                                  (1.6, 0.28, 1.5, -5, (0.30, 0.65, 0.40))):
        m = mat_mul(translate(BOOK_X + 1.05, y + sy / 2.0, 0.0), rotate((0, 1, 0), ry))
        out.append(attr(coated(col, 0.2), emit(rounded_box(sx, sy, sz, 0.06), m)))
        y += sy
    # a window with the night sky in it (an area light), a frame, and a glass paperweight that throws the lamp's light into a caustic
    sky = 'AttributeBegin\n  AreaLightSource "diffuse" "rgb L" [ 0.55 0.70 1.25 ]\n  Material "diffuse" "rgb reflectance" [ 0.1 0.1 0.1 ]\n'
    out.append(sky + emit(box(5.2, 2.8, 0.05), translate(-1.2, 3.9, -7.7)) + "\nAttributeEnd")
    frame_mat = diffuse((0.20, 0.14, 0.10))
    out.append(attr(frame_mat, [emit(box(5.6, 0.22, 0.2), translate(-1.2, 5.31, -7.65)), emit(box(5.6, 0.22, 0.2), translate(-1.2, 2.49, -7.65)),
                                emit(box(0.22, 3.2, 0.2), translate(-3.89, 3.9, -7.65)), emit(box(0.22, 3.2, 0.2), translate(1.49, 3.9, -7.65)),
                                emit(box(0.16, 2.8, 0.2), translate(-1.2, 3.9, -7.6)), emit(box(5.2, 0.16, 0.2), translate(-1.2, 3.9, -7.6))]))
    out.append(attr('Material "dielectric" "float eta" [ 1.5 ]', emit(PARTS["ball"], mat_mul(translate(2.0, 0.5, -1.9), scale(0.5)))))
    # a pencil and a mug at the back
    out.append(attr(coated((0.95, 0.80, 0.15), 0.15), emit(PARTS["arm"], mat_mul(translate(-1.0, 0.07, -3.4), rotate((0, 0, 1), -90), rotate((0, 1, 0), 20), scale(0.07, 2.0, 0.07)))))
    out.append(attr(coated((0.85, 0.85, 0.9), 0.08), emit(lathe([(0.0, 0.0), (0.55, 0.0), (0.58, 0.05), (0.62, 1.0), (0.56, 1.0), (0.0, 0.1)], 40), translate(-5.6, 0.0, -3.6))))
    return "\n".join(out)


def build_ball(t):
    _, pos, vel, spin = ball_state(t)
    m = translate(pos)
    speed = vlen(vel)
    airborne = pos[1] > BALL_R + 0.03
    if airborne and speed > 1.0:
        # stretch along the velocity while it flies
        s = 1.0 + 0.16 * clamp01(speed / 12.0)
        f = align_y((0.0, 0.0, 0.0), vel)
        ft = [f[0], f[4], f[8], 0, f[1], f[5], f[9], 0, f[2], f[6], f[10], 0, 0, 0, 0, 1]
        m = mat_mul(m, f, scale(1.0 / math.sqrt(s), s, 1.0 / math.sqrt(s)), ft)
    m = mat_mul(m, rotate((0, 0, 1), -spin), scale(BALL_R))
    return attr(coated((0.90, 0.10, 0.08), 0.03), emit(PARTS["ball"], m))


def camera(t):
    """The camera drifts in while the lamp sleeps, then follows the action loosely and pulls back at the end."""
    eye = Track([(0.0, (1.6, 3.4, 12.5)), (4.0, (0.9, 3.0, 11.2)), (9.0, (0.8, 3.1, 11.0)), (13.0, (-0.2, 3.0, 11.0)), (16.0, (-1.4, 2.7, 10.2)),
                 (20.0, (-1.4, 3.0, 11.6))], "smooth")(t)
    target = Track([(0.0, (-1.2, 1.2, 0.0)), (4.0, (-0.8, 1.2, 0.0)), (9.0, (0.6, 1.1, 0.0)), (13.0, (-0.2, 1.1, 0.0)), (16.0, (-1.9, 1.0, 0.0)),
                    (20.0, (-1.9, 1.1, 0.0))], "smooth")(t)
    dist = vlen(vsub(eye, target))
    return 'LookAt %s %s 0 1 0\nCamera "perspective" "float fov" [ 34 ] "float lensradius" [ 0.05 ] "float focaldistance" [ %.3f ]' % (fmt3(wp(eye)), fmt3(wp(target)), dist)


def build(t):
    lamp, spot = build_lamp(t)
    lines = [camera(t),
             'Film "rgb" "integer xresolution" [ 1280 ] "integer yresolution" [ 720 ]',
             'Sampler "halton" "integer pixelsamples" [ 128 ]',
             'Integrator "volpath" "integer maxdepth" [ 8 ]',
             "WorldBegin",
             'LightSource "infinite" "rgb L" [ 0.05 0.065 0.11 ]',
             'AttributeBegin\n  AreaLightSource "diffuse" "rgb L" [ 0.9 0.95 1.35 ]\n  Shape "trianglemesh" "integer indices" [ 0 1 2 0 2 3 ] "point3 P" [ -9 9 -3  9 9 -3  9 9 5  -9 9 5 ]\nAttributeEnd',
             spot, build_set(), lamp, build_ball(t)]
    return "\n".join(lines) + "\n"


TITLE_FILTERS = ("fade=t=in:st=0:d=1.2,fade=t=out:st=19.0:d=1.0,"
                 "drawtext=fontfile='C\\:/Windows/Fonts/arialbd.ttf':text='LAMP & BALL':fontsize=h/9:fontcolor=white@0.0:x=(w-text_w)/2:y=h*0.42:"
                 "alpha='if(lt(t,0.4),0,if(lt(t,1.2),(t-0.4)/0.8,if(lt(t,2.4),1,if(lt(t,3.2),(3.2-t)/0.8,0))))':fontcolor=white,"
                 "drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='made with Photonsmith':fontsize=h/30:x=(w-text_w)/2:y=h*0.9:"
                 "alpha='if(lt(t,17.2),0,if(lt(t,18.0),(t-17.2)/0.8,1))':fontcolor=white")


if __name__ == "__main__":
    sys.exit(film_cli(build, DURATION, FPS, name="lamp_and_ball", extra_filters=TITLE_FILTERS))
