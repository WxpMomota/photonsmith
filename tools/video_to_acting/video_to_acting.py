"""Turns a video of someone acting into a film character's acting: their body as a BVH motion capture and their face as a track of ARKit's 52 face shapes.

    python video_to_acting.py VIDEO OUT [--models DIR] [--fps N]      ->  OUT.bvh  OUT.face
    python video_to_acting.py --selftest OUT                          ->  a made-up capture (a wave and a nod, a blink, a spoken "ah"), no video or MediaPipe needed

In a film file (docs/VIDEO_GENERATION.md, "Characters, round 12"):

    rigged  mira  film_assets/library/mira.glb
    mocap   mira  me.bvh  at 2                     # the body (src/shared/cartoon/mocap_bvh.h retargets it onto the model's bones)
    act     mira  me.face at 2                     # the face (src/shared/cartoon/face_track.h): a rigged model's ARKit morphs, a built-in character's feelings

How: Google's MediaPipe (Apache 2.0) finds the body's 33 points in metres (its pose landmarker's world landmarks) and the face's 52 blend shapes (its face landmarker),
frame by frame on this computer; nothing is uploaded. The body's points become a skeleton named as Mixamo names its bones (Hips, Spine, Spine1, Neck, Head, LeftUpLeg,
LeftLeg, LeftFoot, LeftArm, LeftForeArm, LeftHand and the right ones), its rest pose a T-pose with the actor's own bone lengths; each frame's joint turns are found
from the points (the hips and the chest from their four corners, the head from the ears and the nose, the limbs as the turn that takes the rest bone onto the bone) and
written as Z X Y Euler angles. The points are smoothed over five frames (MediaPipe's jitter). The hips do not travel (the world landmarks are about the hips): use the
film's `walk` lines to move the character, or `mocap ... travel` with a capture that has its own.

Needs (scripts/setup_video_to_acting.ps1 makes it): Python 3.10-3.12, mediapipe, opencv-python, numpy, and the two model files pose_landmarker_full.task and
face_landmarker.task in --models (by default this script's folder).
"""
import math
import os
import sys

import numpy as np

# MediaPipe's pose points
NOSE, L_EAR, R_EAR = 0, 7, 8
L_SH, R_SH, L_EL, R_EL, L_WR, R_WR = 11, 12, 13, 14, 15, 16
L_HIP, R_HIP, L_KN, R_KN, L_AN, R_AN, L_TOE, R_TOE = 23, 24, 25, 26, 27, 28, 31, 32

# the skeleton: name, parent, how its rest offset is made from the actor's lengths, the rest direction of the bone it turns (to its child), its kind
JOINTS = ["Hips", "Spine", "Spine1", "Neck", "Head", "LeftArm", "LeftForeArm", "LeftHand", "RightArm", "RightForeArm", "RightHand",
          "LeftUpLeg", "LeftLeg", "LeftFoot", "RightUpLeg", "RightLeg", "RightFoot"]
PARENT = {"Hips": None, "Spine": "Hips", "Spine1": "Spine", "Neck": "Spine1", "Head": "Neck", "LeftArm": "Spine1", "LeftForeArm": "LeftArm", "LeftHand": "LeftForeArm",
          "RightArm": "Spine1", "RightForeArm": "RightArm", "RightHand": "RightForeArm", "LeftUpLeg": "Hips", "LeftLeg": "LeftUpLeg", "LeftFoot": "LeftLeg",
          "RightUpLeg": "Hips", "RightLeg": "RightUpLeg", "RightFoot": "RightLeg"}


def unit(v):
    n = np.linalg.norm(v)
    return v / n if n > 1e-9 else v


def frame(x, y):
    """A rotation whose columns are x (its left), y (up) and z = x cross y (forward), y kept and x made square to it."""
    y = unit(y)
    x = unit(x - y * np.dot(x, y))
    return np.column_stack([x, y, np.cross(x, y)])


def swing(a, b):
    """The smallest turn taking direction a onto direction b."""
    a, b = unit(a), unit(b)
    v, c = np.cross(a, b), float(np.dot(a, b))
    if c < -0.999999:
        axis = unit(np.cross(a, [1.0, 0.0, 0.0] if abs(a[0]) < 0.9 else [0.0, 1.0, 0.0]))
        return 2.0 * np.outer(axis, axis) - np.eye(3)
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + k + k @ k / (1.0 + c)


def euler_zxy(m):
    """Degrees (z, x, y) with m = Rz Rx Ry (the BVH channels Zrotation Xrotation Yrotation)."""
    x = math.asin(max(-1.0, min(1.0, m[2, 1])))
    if abs(m[2, 1]) < 0.99999:
        y, z = math.atan2(-m[2, 0], m[2, 2]), math.atan2(-m[0, 1], m[1, 1])
    else:   # (gimbal lock: all the turn into z)
        y, z = 0.0, math.atan2(m[1, 0], m[0, 0])
    return math.degrees(z), math.degrees(x), math.degrees(y)


def smooth(frames, window=5):
    """Each point averaged over `window` frames round it."""
    out, half = [], window // 2
    for i in range(len(frames)):
        lo, hi = max(0, i - half), min(len(frames), i + half + 1)
        out.append(np.mean(np.stack(frames[lo:hi]), axis=0))
    return out


def lengths(frames):
    """The actor's bone lengths, the medians over the capture."""
    def med(a, b):
        return float(np.median([np.linalg.norm(f[a] - f[b]) for f in frames]))
    mid = lambda f, a, b: 0.5 * (f[a] + f[b])
    spine = float(np.median([np.linalg.norm(mid(f, L_SH, R_SH) - mid(f, L_HIP, R_HIP)) for f in frames]))
    neck = float(np.median([np.linalg.norm(mid(f, L_EAR, R_EAR) - mid(f, L_SH, R_SH)) for f in frames]))
    return {"hips": med(L_HIP, R_HIP), "shoulders": med(L_SH, R_SH), "spine": spine, "neck": neck, "upper": 0.5 * (med(L_SH, L_EL) + med(R_SH, R_EL)),
            "fore": 0.5 * (med(L_EL, L_WR) + med(R_EL, R_WR)), "thigh": 0.5 * (med(L_HIP, L_KN) + med(R_HIP, R_KN)), "shin": 0.5 * (med(L_KN, L_AN) + med(R_KN, R_AN))}


def offsets(L):
    """Each joint's rest offset from its parent (a T-pose: arms out along +-x, legs down; +x the actor's left, +y up, +z forward)."""
    return {"Hips": (0, 0, 0), "Spine": (0, 0.25 * L["spine"], 0), "Spine1": (0, 0.6 * L["spine"], 0), "Neck": (0, 0.15 * L["spine"], 0), "Head": (0, 0.6 * L["neck"], 0),
            "LeftArm": (0.5 * L["shoulders"], 0, 0), "LeftForeArm": (L["upper"], 0, 0), "LeftHand": (L["fore"], 0, 0),
            "RightArm": (-0.5 * L["shoulders"], 0, 0), "RightForeArm": (-L["upper"], 0, 0), "RightHand": (-L["fore"], 0, 0),
            "LeftUpLeg": (0.5 * L["hips"], 0, 0), "LeftLeg": (0, -L["thigh"], 0), "LeftFoot": (0, -L["shin"], 0),
            "RightUpLeg": (-0.5 * L["hips"], 0, 0), "RightLeg": (0, -L["thigh"], 0), "RightFoot": (0, -L["shin"], 0)}


def turns(p):
    """Every joint's turn in the world for one frame's points (BVH space)."""
    hip_mid, sh_mid, ear_mid = 0.5 * (p[L_HIP] + p[R_HIP]), 0.5 * (p[L_SH] + p[R_SH]), 0.5 * (p[L_EAR] + p[R_EAR])
    g = {}
    g["Hips"] = frame(p[L_HIP] - p[R_HIP], sh_mid - hip_mid)
    g["Spine"] = frame(0.5 * (p[L_HIP] - p[R_HIP] + p[L_SH] - p[R_SH]), sh_mid - hip_mid)
    g["Spine1"] = frame(p[L_SH] - p[R_SH], sh_mid - hip_mid)
    head = frame(p[L_EAR] - p[R_EAR], ear_mid - sh_mid)
    face = unit(p[NOSE] - ear_mid)   # (the nose: the head's forward, so a nod shows)
    head = frame(head[:, 0], np.cross(face, head[:, 0]))
    g["Neck"], g["Head"] = g["Spine1"], head
    for side, sh, el, wr, hip, kn, an, toe, out in (("Left", L_SH, L_EL, L_WR, L_HIP, L_KN, L_AN, L_TOE, 1.0), ("Right", R_SH, R_EL, R_WR, R_HIP, R_KN, R_AN, R_TOE, -1.0)):
        g[side + "Arm"] = swing(np.array([out, 0.0, 0.0]), p[el] - p[sh])
        g[side + "ForeArm"] = swing(np.array([out, 0.0, 0.0]), p[wr] - p[el])
        g[side + "Hand"] = g[side + "ForeArm"]
        g[side + "UpLeg"] = swing(np.array([0.0, -1.0, 0.0]), p[kn] - p[hip])
        g[side + "Leg"] = swing(np.array([0.0, -1.0, 0.0]), p[an] - p[kn])
        g[side + "Foot"] = swing(np.array([0.0, -0.35, 1.0]), p[toe] - p[an])
    return g


def write_bvh(path, frames, fps):
    frames = smooth(frames)
    L = lengths(frames)
    off = offsets(L)
    lines = ["HIERARCHY"]

    def joint(name, depth):
        pad = "  " * depth
        kids = [j for j in JOINTS if PARENT[j] == name]
        lines.append(pad + ("ROOT " if PARENT[name] is None else "JOINT ") + name)
        lines.append(pad + "{")
        lines.append(pad + "  OFFSET %.5f %.5f %.5f" % off[name])
        lines.append(pad + ("  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation" if PARENT[name] is None else "  CHANNELS 3 Zrotation Xrotation Yrotation"))
        for k in kids:
            joint(k, depth + 1)
        if not kids:
            end = {"Head": (0, 0.4 * L["neck"], 0), "LeftHand": (0.08, 0, 0), "RightHand": (-0.08, 0, 0), "LeftFoot": (0, -0.04, 0.12), "RightFoot": (0, -0.04, 0.12)}[name]
            lines.append(pad + "  End Site\n" + pad + "  {\n" + pad + "    OFFSET %.5f %.5f %.5f\n" % end + pad + "  }")
        lines.append(pad + "}")
    joint("Hips", 0)
    lines += ["MOTION", "Frames: %d" % len(frames), "Frame Time: %.6f" % (1.0 / fps)]
    standing = L["thigh"] + L["shin"] + 0.06
    for p in frames:
        g = turns(p)
        row = [0.0, standing, 0.0]
        for name in JOINTS:
            parent = PARENT[name]
            local = g[name] if parent is None else g[parent].T @ g[name]
            z, x, y = euler_zxy(local)
            row += [z, x, y]
        lines.append(" ".join("%.4f" % v for v in row))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def write_face(path, times, shapes, names):
    with open(path, "w") as f:
        f.write("# a face track: ARKit's blend shapes over time (tools/video_to_acting/video_to_acting.py)\n")
        f.write("names " + " ".join(names) + "\n")
        for t, row in zip(times, shapes):
            f.write("%.4f " % t + " ".join("%.3f" % v for v in row) + "\n")


def to_bvh_space(points):
    """MediaPipe's world landmarks (x to the image's right, y down, z away from the camera; metres about the hips) as BVH's: +x the actor's left (they face the camera),
    +y up, +z towards the camera."""
    return np.array([[x, -y, -z] for x, y, z in points])


def capture(video, models, fps_out):
    import cv2
    import mediapipe as mp
    from mediapipe.tasks import python as mpt
    from mediapipe.tasks.python import vision
    pose = vision.PoseLandmarker.create_from_options(vision.PoseLandmarkerOptions(base_options=mpt.BaseOptions(model_asset_path=os.path.join(models, "pose_landmarker_full.task")),
                                                                                 running_mode=vision.RunningMode.VIDEO))
    face = vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(base_options=mpt.BaseOptions(model_asset_path=os.path.join(models, "face_landmarker.task")),
                                                                                 running_mode=vision.RunningMode.VIDEO, output_face_blendshapes=True))
    cap = cv2.VideoCapture(video)
    if not cap.isOpened():
        raise SystemExit("cannot open the video " + video)
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    every = max(1, int(round(fps / fps_out))) if fps_out else 1
    body, times, shapes, names, last_body, last_face, n = [], [], [], None, None, None, 0
    while True:
        ok, img = cap.read()
        if not ok:
            break
        if n % every == 0:
            t_ms = int(n * 1000.0 / fps)
            image = mp.Image(image_format=mp.ImageFormat.SRGB, data=cv2.cvtColor(img, cv2.COLOR_BGR2RGB))
            r = pose.detect_for_video(image, t_ms)
            if r.pose_world_landmarks:
                last_body = to_bvh_space([(q.x, q.y, q.z) for q in r.pose_world_landmarks[0]])
            if last_body is not None:
                body.append(last_body)
            f = face.detect_for_video(image, t_ms)
            if f.face_blendshapes:
                cats = [c for c in f.face_blendshapes[0] if c.category_name != "_neutral"]
                names = names or [c.category_name for c in cats]
                last_face = [c.score for c in cats]
            if last_face is not None:
                times.append(n / fps)
                shapes.append(last_face)
            if total:
                print("PROGRESS %d frame %d of %d" % (int(100 * n / total), n, total), flush=True)
        n += 1
    if not body:
        raise SystemExit("no one was found in the video (the whole body should be in the picture)")
    return body, (fps / every), times, shapes, names


def selftest():
    """A made-up actor: standing, waving the right arm from 0.5 s, nodding at 2 s; a face that blinks at 1 s and says "ah" at 1.5 s."""
    rest = {NOSE: (0, 0.60, 0.10), L_EAR: (0.08, 0.58, 0.0), R_EAR: (-0.08, 0.58, 0.0), L_SH: (0.19, 0.45, 0), R_SH: (-0.19, 0.45, 0), L_EL: (0.21, 0.17, 0),
            R_EL: (-0.21, 0.17, 0), L_WR: (0.23, -0.08, 0), R_WR: (-0.23, -0.08, 0), L_HIP: (0.1, 0, 0), R_HIP: (-0.1, 0, 0), L_KN: (0.1, -0.42, 0.02),
            R_KN: (-0.1, -0.42, 0.02), L_AN: (0.1, -0.84, 0), R_AN: (-0.1, -0.84, 0), L_TOE: (0.1, -0.9, 0.14), R_TOE: (-0.1, -0.9, 0.14)}
    fps, body, times, shapes = 30.0, [], [], []
    names = ["eyeBlinkLeft", "eyeBlinkRight", "jawOpen", "mouthSmileLeft", "mouthSmileRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight", "mouthFunnel"]
    for i in range(int(3.0 * fps)):
        t = i / fps
        p = np.zeros((33, 3))
        for k, v in rest.items():
            p[k] = v
        wave = min(1.0, max(0.0, (t - 0.5) / 0.4)) * (1.0 - min(1.0, max(0.0, (t - 2.4) / 0.4)))
        sh = p[R_SH]
        up = np.array([-0.08 - 0.05 * math.sin(2 * math.pi * 2.0 * t), 0.28, 0.02])   # (the forearm up, waving)
        p[R_EL] = sh + (1 - wave) * (np.array(rest[R_EL]) - sh) + wave * np.array([-0.27, 0.05, 0.03])
        p[R_WR] = p[R_EL] + (1 - wave) * (np.array(rest[R_WR]) - np.array(rest[R_EL])) + wave * unit(up) * 0.25
        nod = math.sin(math.pi * min(1.0, max(0.0, (t - 2.0) / 0.5))) * 0.06
        p[NOSE] = p[NOSE] + np.array([0, -nod, 0.02 * nod / 0.06])
        body.append(p)
        blink = max(0.0, 1.0 - abs(t - 1.0) / 0.08)
        ah = max(0.0, 1.0 - abs(t - 1.6) / 0.3)
        times.append(t)
        shapes.append([blink, blink, 0.8 * ah, 0.3 + 0.3 * wave, 0.3 + 0.3 * wave, 0.4 * nod / 0.06, 0.2 * wave, 0.2 * wave, 0.0])
    return body, fps, times, shapes, names


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "--selftest":
        body, fps, times, shapes, names = selftest()
        out = args[1]
    else:
        if len(args) < 2:
            raise SystemExit(__doc__)
        video, out = args[0], args[1]
        models = args[args.index("--models") + 1] if "--models" in args else os.path.dirname(os.path.abspath(__file__))
        fps_out = float(args[args.index("--fps") + 1]) if "--fps" in args else 0.0
        body, fps, times, shapes, names = capture(video, models, fps_out)
    write_bvh(out + ".bvh", body, fps)
    if names:
        write_face(out + ".face", times, shapes, names)
    print("wrote %s.bvh (%d frames at %.1f a second)%s" % (out, len(body), fps, (" and %s.face (%d shapes)" % (out, len(names))) if names else ""))


if __name__ == "__main__":
    main()
