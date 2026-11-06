"""Check portable Nomai OBJ/MTL exports, including inverse-transformed normals.

Usage: python scripts/anim/check_nomai_exports.py MODEL_DIR
Run nomai_asset_review first. This checks serialized output, not source buffers.
"""
import math
from pathlib import Path
import sys


def check(path):
    vertices, normals, faces, used, library = [], [], [], set(), None
    for line in path.read_text().splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] in ('v', 'vn'):
            point = tuple(map(float, fields[1:]))
            assert len(point) == 3 and all(math.isfinite(x) for x in point), path
            (vertices if fields[0] == 'v' else normals).append(point)
        elif fields[0] == 'f':
            faces.append(fields[1:])
        elif fields[0] == 'usemtl':
            used.add(fields[1])
        elif fields[0] == 'mtllib':
            library = path.parent / fields[1]
    assert vertices and normals and faces and library and library.is_file(), path
    assert len(vertices) == len(normals), path
    assert all(abs(sum(x*x for x in n)-1.0) < 2e-5 for n in normals), path
    materials = {line.split()[1] for line in library.read_text().splitlines() if line.startswith('newmtl ')}
    assert used <= materials, path
    for face in faces:
        assert len(face) == 3, path
        corners = []
        for token in face:
            indices = token.split('/')
            assert len(indices) == 3 and indices[1] == '', (path, token)
            vi, ni = int(indices[0]), int(indices[2])
            assert 1 <= vi <= len(vertices) and 1 <= ni <= len(normals), (path, token)
            corners.append(vertices[vi-1])
        a, b, c = corners
        ab, ac = [b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)]
        cross = (ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0])
        assert sum(x*x for x in cross) > 1e-24, (path, face)
    print(f'PASS {path.name}: {len(faces)} triangles, finite unit normals, valid material/index references')


if __name__ == '__main__':
    folder = Path(sys.argv[1])
    files = sorted(folder.glob('*.obj'))
    assert len(files) == 9, f'Expected nine catalogue models, found {len(files)}'
    for obj in files:
        check(obj)
