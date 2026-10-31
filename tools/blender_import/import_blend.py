#!/usr/bin/env python3
"""Import a Blender scene as a pbrt scene for this renderer: finds Blender and runs blend_to_pbrt.py inside it.

import_blend.py SCENE.blend [OUT_DIR] [--blender PATH] [--name NAME] [--frame N] [--search DIR] [--hide COLLECTION] [--no-bake]

OUT_DIR defaults to a folder named after the scene beside the .blend. The GUI's File > Import Blender scene does the same and adds it to My Scenes.
"""
import glob, os, shutil, subprocess, sys


def find_blender(given=None):
    """The Blender to run: `given`, BLENDER in the environment, the PATH, then the usual places on Windows (the installer's and Steam's) and macOS."""
    for path in (given, os.environ.get('BLENDER')):
        if path and os.path.isfile(path):
            return path
    found = shutil.which('blender')
    if found:
        return found
    candidates = sorted(glob.glob(r'C:\Program Files\Blender Foundation\*\blender.exe'), reverse=True)
    candidates += [r'C:\Program Files (x86)\Steam\steamapps\common\Blender\blender.exe', r'C:\Program Files\Steam\steamapps\common\Blender\blender.exe',
                   '/Applications/Blender.app/Contents/MacOS/Blender', os.path.expanduser('~/Applications/Blender.app/Contents/MacOS/Blender')]
    return next((c for c in candidates if os.path.isfile(c)), None)


def command(blender, blend, out, script, extra):
    return [blender, '--background', '--factory-startup', '--disable-autoexec', '-noaudio', blend, '--python', script, '--', out] + extra


def main(argv):
    args = list(argv)
    blender = None
    if '--blender' in args:
        at = args.index('--blender')
        blender = args[at + 1]
        del args[at:at + 2]
    if not args or args[0].startswith('-'):
        print(__doc__)
        return 2
    blend = os.path.abspath(args.pop(0))
    if not os.path.isfile(blend):
        print(f'no such file: {blend}')
        return 1
    out = os.path.abspath(args.pop(0)) if args and not args[0].startswith('-') else os.path.splitext(blend)[0]
    blender = find_blender(blender)
    if not blender:
        print('Blender was not found: install it, or say where it is with --blender PATH or the BLENDER environment variable')
        return 1
    os.makedirs(out, exist_ok=True)
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'blend_to_pbrt.py')
    done = False
    with subprocess.Popen(command(blender, blend, out, script, args), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace') as p:
        for line in p.stdout:
            if line.startswith(('STEP ', 'MISSING ')) or 'Traceback' in line or 'Error:' in line:
                print(line.rstrip(), flush=True)
            done = done or line.startswith('STEP done')
    if p.returncode != 0 or not done:
        print(f'the import did not finish (Blender exit code {p.returncode})')
        return 1
    print(f'wrote {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
