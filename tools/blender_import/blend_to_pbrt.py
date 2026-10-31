"""Convert a Blender scene to a pbrt-v4 scene for this renderer. Run inside Blender (import_blend.py or the GUI's File > Import Blender scene find it and run it for you):

blender --background --factory-startup --disable-autoexec scene.blend --python blend_to_pbrt.py -- OUT_DIR [--name NAME] [--frame N] [--search DIR] [--hide COLLECTION] [--no-bake]

--disable-autoexec keeps a .blend from running scripts of its own. Writes OUT_DIR/NAME.pbrt, geometry/ and textures/; pictures the .blend cannot find are looked for by name under
--search (default: the .blend's folder). --hide leaves a collection out and can be given several times.

Meshes are written with their modifiers applied, one binary PLY per (object, material). A material whose colour comes from nodes (colour ramps over coordinates, painted pictures,
mixes) is baked to a picture with Cycles on a fresh smart-UV layout; a plain one keeps its values; a mix with a transparent shader becomes a cutout. Particle and geometry-node
copies become ObjectInstances, hair drawn as paths pbrt curves. Lights, world colour and camera (lens, depth of field) follow Blender's units: point and spot I = P / (4 pi),
emission L = colour * strength, an area light (which pbrt would show to the camera) a wide spot of the panel's intensity P / pi.

Blender's y and z are swapped, to pbrt's y up and left hand: film files and the cartoon camera use the scene as it is. The file's view transform becomes the scene's tone map
(`# @rt-look tonemap filmic`). A scene made for Eevee lit it with lamps, not with its glowing meshes: those stay a glow the camera sees, lighting nothing, and do not shadow the lamps
inside them; a Cycles scene keeps them as real lights.
"""
import bpy, bmesh, os, sys, math, struct, time
import numpy as np
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
OUT = argv[0]
def option(name, default=None):
    return argv[argv.index(name) + 1] if name in argv and argv.index(name) + 1 < len(argv) else default
NAME = option('--name', os.path.splitext(os.path.basename(bpy.data.filepath))[0] or 'scene')
ROOT = option('--search', os.path.dirname(bpy.data.filepath))
FRAME = int(option('--frame', '1'))
NO_BAKE = '--no-bake' in argv
HIDE = [argv[i + 1] for i, a in enumerate(argv[:-1]) if a == '--hide']
# pbrt's usual frame, y up: (x, y, z) -> (x, z, y). Swapping two axes also turns Blender's right-handed space into pbrt's left-handed one, so the camera needs no mirror.
SW = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
def swp(v): return SW.to_3x3() @ Vector(v)

def remap_images(root):
    """Point every picture the .blend cannot find at a file of the same name under `root` (a .blend keeps paths from the machine it was made on)."""
    index = {}
    for d, _, fs in os.walk(root):
        for f in fs: index.setdefault(f.lower(), os.path.join(d, f))
    missing = []
    for im in bpy.data.images:
        if im.source != 'FILE' or im.packed_file or os.path.exists(bpy.path.abspath(im.filepath)): continue
        base = os.path.basename(im.filepath.replace('\\', '/')).lower()
        if base in index:
            im.filepath = index[base]
            im.reload()
        else:
            missing.append(im.name)
    return missing

os.makedirs(os.path.join(OUT, 'geometry'), exist_ok=True)
os.makedirs(os.path.join(OUT, 'textures'), exist_ok=True)
log = open(os.path.join(OUT, 'export_log.txt'), 'w', encoding='utf-8')
def p(*a):
    print(*a, file=log); log.flush()

print('STEP reading the scene', flush=True)
missing_images = remap_images(ROOT)
p('missing images', missing_images)
for c in bpy.data.collections:
    if c.name in HIDE:
        c.hide_render = True
        for o in c.all_objects:
            if o is not None: o.hide_render = True
EEVEE = bpy.context.scene.render.engine != 'CYCLES'   # (a scene made for Eevee: its glowing meshes do not light it)
sc = bpy.context.scene
# the depsgraph Python gets is the viewport's: give the viewport what the render would use (visibility, subdivision levels, particle amounts)
def render_visibility(layer_coll):
    layer_coll.hide_viewport = False
    layer_coll.collection.hide_viewport = layer_coll.collection.hide_render
    for ch in layer_coll.children: render_visibility(ch)
render_visibility(bpy.context.view_layer.layer_collection)
for o in bpy.data.objects:
    o.hide_viewport = o.hide_render
    o.hide_set(False)
    for md in o.modifiers:
        md.show_viewport = md.show_render
        if md.type == 'SUBSURF': md.levels = md.render_levels
        if md.type == 'MULTIRES': md.levels = md.render_levels
for ps in bpy.data.particles:
    ps.display_percentage = 100
    if ps.child_type != 'NONE': ps.child_percent = ps.rendered_child_count
sc.frame_set(FRAME)
p('view', sc.view_settings.view_transform, sc.view_settings.look, 'exposure', sc.view_settings.exposure, 'gamma', sc.view_settings.gamma)

def safe(s):
    out = []
    for ch in s:
        out.append(ch if (ch.isascii() and (ch.isalnum() or ch in '-_.')) else f'u{ord(ch):x}')
    return ''.join(out)

# ---------------------------------------------------------------- materials
def principled_of(mat):
    if not mat or not mat.use_nodes: return None
    out = next((n for n in mat.node_tree.nodes if n.bl_idname == 'ShaderNodeOutputMaterial' and n.is_active_output), None) \
        or next((n for n in mat.node_tree.nodes if n.bl_idname == 'ShaderNodeOutputMaterial'), None)
    if not out or not out.inputs['Surface'].links: return None
    return out.inputs['Surface'].links[0].from_node

def surface_kind(mat):
    n = principled_of(mat)
    if n is None: return 'none', None
    t = n.bl_idname
    if t == 'ShaderNodeBsdfPrincipled': return 'principled', n
    if t == 'ShaderNodeEmission': return 'emission', n
    if t == 'ShaderNodeBsdfDiffuse': return 'diffuse', n
    if t == 'ShaderNodeMixShader': return 'mix', n
    return t, n

def colour_linked(mat):
    kind, n = surface_kind(mat)
    if kind == 'principled':
        return n.inputs['Base Color'].is_linked or n.inputs['Emission Color'].is_linked
    if kind == 'diffuse': return n.inputs['Color'].is_linked
    if kind == 'emission': return n.inputs['Color'].is_linked
    if kind == 'mix': return True
    return False

def lin3(c): return (max(0.0, c[0]), max(0.0, c[1]), max(0.0, c[2]))

def material_info(mat):
    """(kind, base rgb, roughness, metallic, emission rgb*strength, alpha)"""
    kind, n = surface_kind(mat)
    info = dict(kind=kind, base=(0.6, 0.6, 0.6), rough=0.5, metal=0.0, emit=(0, 0, 0), alpha=1.0)
    if mat is None: return info
    if kind == 'principled':
        info['base'] = lin3(n.inputs['Base Color'].default_value)
        info['rough'] = n.inputs['Roughness'].default_value
        info['metal'] = n.inputs['Metallic'].default_value
        s = n.inputs['Emission Strength'].default_value
        c = n.inputs['Emission Color'].default_value
        if s > 0 and (c[0] + c[1] + c[2]) > 0: info['emit'] = tuple(x * s for x in lin3(c))
        info['alpha'] = n.inputs['Alpha'].default_value if not n.inputs['Alpha'].is_linked else -1
    elif kind == 'emission':
        s = n.inputs['Strength'].default_value
        info['emit'] = tuple(x * s for x in lin3(n.inputs['Color'].default_value))
        info['base'] = (0, 0, 0)
    elif kind == 'diffuse':
        info['base'] = lin3(n.inputs['Color'].default_value); info['rough'] = 1.0
    elif kind == 'mix':
        # a mix with a transparent shader: take the non-transparent side; the factor is the cutout (baked as alpha)
        for i in (1, 2):
            if n.inputs[i].links:
                m = n.inputs[i].links[0].from_node
                if m.bl_idname == 'ShaderNodeBsdfPrincipled':
                    info['base'] = lin3(m.inputs['Base Color'].default_value); info['rough'] = m.inputs['Roughness'].default_value
                elif m.bl_idname == 'ShaderNodeBsdfTransparent':
                    info['alpha'] = -1
    return info

# ---------------------------------------------------------------- collect what is rendered
dg = bpy.context.evaluated_depsgraph_get()
plain = []        # (evaluated object, world matrix)
instances = {}    # source original name -> [matrices]
inst_src = {}
for inst in dg.object_instances:
    ob = inst.object
    if ob.type not in ('MESH', 'CURVE', 'SURFACE', 'META', 'FONT'): continue
    if inst.is_instance:
        src = ob.original
        instances.setdefault(src.name, []).append(inst.matrix_world.copy())
        inst_src[src.name] = src
    else:
        if ob.original.hide_render: continue
        plain.append((ob.original, inst.matrix_world.copy()))
p('plain objects', len(plain), 'instanced sources', {k: len(v) for k, v in instances.items()})

# ---------------------------------------------------------------- applied meshes (all made first, then linked: linking re-evaluates the depsgraph)
def applied_mesh(orig):
    me = bpy.data.meshes.new_from_object(orig.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
    if orig.type == 'MESH':
        me.use_auto_texspace = False
        me.texspace_location = orig.data.texspace_location
        me.texspace_size = orig.data.texspace_size
    return me
pending = []
for i, (orig, m) in enumerate(plain):
    pending.append((applied_mesh(orig), m, f'W{i}', safe(orig.name) + f'_{i}', False, orig.name))
for name, orig in inst_src.items():
    pending.append((applied_mesh(orig), Matrix.Identity(4), f'S_{len(pending)}', 'inst_' + safe(name), True, name))
hair_objects = [(e.original, e.matrix_world.copy()) for e in dg.objects
                if e.type == 'MESH' and not e.original.hide_render and any(ps.settings.type == 'HAIR' and ps.settings.render_type == 'PATH' for ps in e.particle_systems)]
p('hair objects', [o.name for o, _ in hair_objects])
def image_mean(mat):
    if not mat or not mat.node_tree: return None
    for n in mat.node_tree.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image and n.image.size[0]:
            px = np.empty(len(n.image.pixels), np.float32); n.image.pixels.foreach_get(px)
            c = px.reshape(-1, 4)[:, :3]
            if n.image.colorspace_settings.name.lower().startswith('srgb'): c = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)   # pixels are stored encoded
            return tuple(float(x) for x in c.mean(0))
    return None
hair_strands = []
for orig, m in hair_objects:
    ob = orig.evaluated_get(dg)
    for psys in ob.particle_systems:
        st = psys.settings
        if st.type != 'HAIR' or st.render_type != 'PATH': continue
        total = len(psys.particles) + len(psys.child_particles)
        steps = 4
        width = 2 * st.root_radius * st.radius_scale
        tip = 2 * st.tip_radius * st.radius_scale
        mi = st.material - 1
        mat = orig.material_slots[mi].material if 0 <= mi < len(orig.material_slots) else None
        colour = image_mean(mat) or material_info(mat)['base']
        pts_all = []
        for i in range(total):
            pts = [psys.co_hair(ob, particle_no=i, step=s) for s in range(steps + 1)]
            if all(v.length == 0 for v in pts): continue
            pts_all.append(pts)
        p('hair read', orig.name, psys.name, total, 'strands asked', len(pts_all), 'read')
        if pts_all: hair_strands.append((orig.name + '_' + psys.name, mat, colour, width, tip, pts_all))

work = bpy.data.collections.new('pbrt_export_work')
sc.collection.children.link(work)
jobs = []   # (work object, key, is instance source, source name)
for me, m, oname, key, is_src, name in pending:
    o = bpy.data.objects.new(oname, me)
    o.matrix_world = m
    work.objects.link(o)
    jobs.append((o, key, is_src, name))
# keep the originals out of the bake (only the work copies are selected, but hide them anyway so nothing else is evaluated twice)
was_hidden = {c.name: c.hide_render for c in sc.collection.children}
for c in sc.collection.children:
    if c != work: c.hide_render = True

# ---------------------------------------------------------------- bake
def world_area(o):
    me = o.data
    me.calc_loop_triangles()
    if not len(me.loop_triangles): return 0.0
    s = o.matrix_world.to_3x3()
    det = abs(s.determinant()) ** (2.0 / 3.0)
    return sum(t.area for t in me.loop_triangles) * det

def needs_bake(o):
    return any(colour_linked(s.material) for s in o.material_slots if s.material)

sc.render.engine = 'CYCLES'
sc.cycles.samples = 4
sc.cycles.device = 'GPU'
try:
    prefs = bpy.context.preferences.addons['cycles'].preferences
    prefs.compute_device_type = 'OPTIX'
    prefs.get_devices()
    for d in prefs.devices: d.use = True
except Exception as e:
    p('gpu', e); sc.cycles.device = 'CPU'
sc.render.bake.margin = 4

baked = {}   # key -> texture file
alpha_baked = {}
t0 = time.time()
for job_index, (o, key, is_src, src_name) in enumerate(jobs):
    print(f'STEP baking materials {job_index + 1} of {len(jobs)}', flush=True)
    if NO_BAKE or not o.data.polygons or not needs_bake(o): continue
    me = o.data
    uv = me.uv_layers.new(name='pbrt_bake')
    if uv is None: p('no uv layer room', key); continue
    render_uv = next((l for l in me.uv_layers if l.active_render), None)
    me.uv_layers.active = uv
    if render_uv and render_uv != uv: render_uv.active_render = True
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.004, area_weight=0.0, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    area = world_area(o) if not is_src else world_area(o) * 4
    res = int(min(2048, max(128, 2 ** round(math.log2(max(1.0, math.sqrt(area) * 512))))))
    img = bpy.data.images.new('bake_' + key, res, res, alpha=False, float_buffer=False)
    nodes_added = []
    for s in o.material_slots:
        if not s.material or not s.material.use_nodes: continue
        nt = s.material.node_tree
        tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = img
        nt.nodes.active = tn; nodes_added.append((nt, tn))
    emit_only = all(surface_kind(s.material)[0] == 'emission' for s in o.material_slots if s.material)
    try:
        if emit_only:
            # bake the colour at strength 1 (an 8-bit picture clips colour x strength); the strength goes on the light's "scale"
            strengths = []
            for s in o.material_slots:
                kind, n = surface_kind(s.material)
                if kind == 'emission':
                    strengths.append((n, n.inputs['Strength'].default_value)); n.inputs['Strength'].default_value = 1.0
            bpy.ops.object.bake(type='EMIT', margin=4, use_clear=True)
            for n, v in strengths: n.inputs['Strength'].default_value = v
        else:
            bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'}, margin=4, use_clear=True)
        f = 'textures/' + key + '.png'
        img.filepath_raw = os.path.join(OUT, f); img.file_format = 'PNG'; img.save()
        baked[key] = f
    except Exception as e:
        p('bake failed', key, e)
    # a mix with a transparent shader (the dappled-light gobos under the spots): bake the mix factor as a cutout, 1 = solid
    cut = []
    for s in o.material_slots:
        m = s.material
        kind, n = surface_kind(m)
        if kind != 'mix': continue
        sides = [n.inputs[i].links[0].from_node.bl_idname if n.inputs[i].links else None for i in (1, 2)]
        if 'ShaderNodeBsdfTransparent' not in sides: continue
        cut.append((m.node_tree, n, sides.index('ShaderNodeBsdfTransparent') == 1))
    if cut:
        aimg = bpy.data.images.new('alpha_' + key, res, res, alpha=False, float_buffer=False)
        aimg.colorspace_settings.name = 'Non-Color'
        undo = []
        for nt, n, transparent_is_second in cut:
            outn = next(x for x in nt.nodes if x.bl_idname == 'ShaderNodeOutputMaterial' and x.inputs['Surface'].links and x.inputs['Surface'].links[0].from_node == n)
            em = nt.nodes.new('ShaderNodeEmission')
            if n.inputs[0].links: nt.links.new(n.inputs[0].links[0].from_socket, em.inputs['Color'])
            else: em.inputs['Color'].default_value = (n.inputs[0].default_value,) * 3 + (1,)
            nt.links.new(em.outputs[0], outn.inputs['Surface'])
            tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = aimg; nt.nodes.active = tn
            undo.append((nt, n, outn, em, tn, transparent_is_second))
        try:
            bpy.ops.object.bake(type='EMIT', margin=4, use_clear=True)
            px = np.empty(res * res * 4, np.float32); aimg.pixels.foreach_get(px)
            if all(u[5] for u in undo): px[0::4] = 1 - px[0::4]; px[1::4] = 1 - px[1::4]; px[2::4] = 1 - px[2::4]
            aimg.pixels.foreach_set(px)
            f = 'textures/' + key + '_alpha.png'
            aimg.filepath_raw = os.path.join(OUT, f); aimg.file_format = 'PNG'; aimg.save()
            alpha_baked[key] = f
        except Exception as e:
            p('alpha bake failed', key, e)
        for nt, n, outn, em, tn, _ in undo:
            nt.links.new(n.outputs[0], outn.inputs['Surface'])
            nt.nodes.remove(em); nt.nodes.remove(tn)
    for nt, tn in nodes_added: nt.nodes.remove(tn)
p('baked', len(baked), 'cutouts', len(alpha_baked), 'in', round(time.time() - t0, 1), 's')

# ---------------------------------------------------------------- geometry
def write_ply(path, P, N, UV, F):
    with open(path, 'wb') as fh:
        hdr = ['ply', 'format binary_little_endian 1.0', f'element vertex {len(P)}', 'property float x', 'property float y', 'property float z',
               'property float nx', 'property float ny', 'property float nz']
        if UV is not None: hdr += ['property float u', 'property float v']
        hdr += [f'element face {len(F)}', 'property list uchar int vertex_indices', 'end_header']
        fh.write(('\n'.join(hdr) + '\n').encode())
        cols = [P, N] + ([UV] if UV is not None else [])
        np.hstack(cols).astype('<f4').tofile(fh)
        faces = np.empty(len(F), dtype=[('n', 'u1'), ('i', '<i4', 3)])
        faces['n'] = 3; faces['i'] = F
        faces.tofile(fh)

def mesh_parts(o, world):
    me = o.data
    me.calc_loop_triangles()
    nt = len(me.loop_triangles)
    if nt == 0: return {}
    tri_loops = np.empty(nt * 3, np.int32); me.loop_triangles.foreach_get('loops', tri_loops)
    tri_mat = np.empty(nt, np.int32); me.loop_triangles.foreach_get('material_index', tri_mat)
    loop_vert = np.empty(len(me.loops), np.int32); me.loops.foreach_get('vertex_index', loop_vert)
    co = np.empty(len(me.vertices) * 3, np.float32); me.vertices.foreach_get('co', co); co = co.reshape(-1, 3)
    cn = np.empty(len(me.loops) * 3, np.float32); me.corner_normals.foreach_get('vector', cn); cn = cn.reshape(-1, 3)
    uvl = me.uv_layers.get('pbrt_bake') or next((l for l in me.uv_layers if l.active_render), None)
    uv = None
    if uvl:
        uv = np.empty(len(me.loops) * 2, np.float32); uvl.data.foreach_get('uv', uv); uv = uv.reshape(-1, 2)
    if world:
        M = np.array(SW @ o.matrix_world, dtype=np.float64)
        co = (co @ M[:3, :3].T + M[:3, 3]).astype(np.float32)
        Ninv = np.linalg.inv(M[:3, :3]).T
        cn = cn @ Ninv.T
        cn /= np.maximum(np.linalg.norm(cn, axis=1, keepdims=True), 1e-12)
        cn = cn.astype(np.float32)
        flip = np.linalg.det(M[:3, :3]) < 0
    else:
        flip = False
    parts = {}
    for mi in np.unique(tri_mat):
        sel = np.where(tri_mat == mi)[0]
        L = tri_loops.reshape(-1, 3)[sel]
        if flip: L = L[:, ::-1]
        flatL = L.reshape(-1)
        keyrows = np.hstack([loop_vert[flatL][:, None].astype(np.float32), cn[flatL]] + ([uv[flatL]] if uv is not None else []))
        uniq, inv = np.unique(keyrows, axis=0, return_inverse=True)
        first = np.zeros(len(uniq), np.int64); first[inv] = np.arange(len(flatL))
        ul = flatL[first]
        P = co[loop_vert[ul]]; N = cn[ul]; UV = uv[ul] if uv is not None else None
        parts[int(mi)] = (P, N, UV, inv.reshape(-1, 3).astype(np.int32))
    return parts

# ---------------------------------------------------------------- pbrt text
def f3(v): return ' '.join(f'{x:.6g}' for x in v)
def mat16(M): return ' '.join(f'{M[r][c]:.8g}' for c in range(4) for r in range(4))   # column by column, as pbrt reads it

lines_world = []
named = {}
def material_name(mat, key, mi, tex):
    info = material_info(mat)
    base_name = safe(mat.name) if mat else 'default'
    nm = base_name + (f'__{key}' if tex else '')
    if nm in named: return nm, info
    named[nm] = True
    if tex:
        lines_world.append(f'Texture "{nm}_tex" "spectrum" "imagemap" "string filename" "{tex}" "string wrap" "clamp"')
        refl = f'"texture reflectance" "{nm}_tex"'
    else:
        refl = f'"rgb reflectance" [ {f3(info["base"])} ]'
    r = max(0.03, info['rough'])
    if info['metal'] > 0.5:
        lines_world.append(f'MakeNamedMaterial "{nm}" "string type" "conductor" "float roughness" {r:.4g} '
                           f'"spectrum eta" "metal-Al-eta" "spectrum k" "metal-Al-k"')
    elif r >= 0.85:
        lines_world.append(f'MakeNamedMaterial "{nm}" "string type" "diffuse" {refl}')
    else:
        lines_world.append(f'MakeNamedMaterial "{nm}" "string type" "coateddiffuse" {refl} "float roughness" {r:.4g}')
    return nm, info

obj_defs = []
shape_lines = []
emitters = 0
for o, key, is_src, src_name in jobs:
    parts = mesh_parts(o, world=not is_src)
    tex = baked.get(key)
    block = []
    for mi, (P, N, UV, F) in parts.items():
        mat = o.material_slots[mi].material if mi < len(o.material_slots) else None
        info = material_info(mat)
        f = 'geometry/' + (f'{key}_m{mi}.ply')
        write_ply(os.path.join(OUT, f), P, N, UV if (tex or (mat and colour_linked(mat))) else None, F)
        if any(c > 0 for c in info['emit']) or (info['kind'] == 'emission' and tex):
            emitters += 1
            link, group = (' "bool sampled" false "string illuminates" [ "glow" ]', ' "string linkgroup" "glow"') if EEVEE else ('', '')
            if tex:
                block.append(f'AttributeBegin\n  AreaLightSource "diffuse" "string filename" "{tex}" "float scale" {max(1e-6, max(info["emit"]) if max(info["emit"]) > 0 else 1):.6g}{link}\n'
                             f'  Material "diffuse" "rgb reflectance" [0 0 0]\n  Shape "plymesh" "string filename" "{f}"{group}\nAttributeEnd')
            else:
                block.append(f'AttributeBegin\n  AreaLightSource "diffuse" "rgb L" [ {f3(info["emit"])} ]{link}\n'
                             f'  Material "diffuse" "rgb reflectance" [ {f3(info["base"])} ]\n  Shape "plymesh" "string filename" "{f}"{group}\nAttributeEnd')
            continue
        nm, _ = material_name(mat, key, mi, tex if (mat and colour_linked(mat)) else None)
        alpha = ''
        if key in alpha_baked and info['alpha'] == -1:
            if f'{key}_alpha' not in named:
                named[f'{key}_alpha'] = True
                lines_world.append(f'Texture "{key}_alpha" "float" "imagemap" "string filename" "{alpha_baked[key]}" "string encoding" "linear" "string wrap" "clamp"')
            alpha = f' "texture alpha" "{key}_alpha"'
        block.append(f'AttributeBegin\n  NamedMaterial "{nm}"\n  Shape "plymesh" "string filename" "{f}"{alpha}\nAttributeEnd')
    if is_src:
        obj_defs.append(f'ObjectBegin "{key}"\n' + '\n'.join(block) + '\nObjectEnd')
        for M in instances[src_name]:
            shape_lines.append(f'AttributeBegin\n  ConcatTransform [ {mat16(SW @ M)} ]\n  ObjectInstance "{key}"\nAttributeEnd')
    else:
        shape_lines.extend(block)
p('emitting shapes', emitters)

# ---------------------------------------------------------------- hair drawn as paths -> curves (strands read before the scene was changed)
curve_lines = []
for c in sc.collection.children:
    if c != work: c.hide_render = was_hidden.get(c.name, False)
dg = bpy.context.evaluated_depsgraph_get()
dg.update()
for hname, mat, colour, width, tip, pts_all in hair_strands:
    f = 'geometry/hair_' + safe(hname) + '.pbrt'
    nm = 'hair_' + safe(hname)
    lines_world.append(f'MakeNamedMaterial "{nm}" "string type" "diffuse" "rgb reflectance" [ {f3(colour)} ]')
    with open(os.path.join(OUT, f), 'w', encoding='utf-8') as fh:
        fh.write(f'NamedMaterial "{nm}"\n')
        for pts in pts_all:
            pts = [swp(v) for v in pts]
            cps = [pts[0]]
            for a, b in zip(pts[:-1], pts[1:]):
                cps += [a.lerp(b, 1 / 3), a.lerp(b, 2 / 3), b]
            flat = ' '.join(f3(v) for v in cps)
            fh.write(f'Shape "curve" "string type" "flat" "point3 P" [ {flat} ] "float width0" {width:.5g} "float width1" {max(tip, width * 0.15):.5g}\n')
    curve_lines.append(f'Include "{f}"')
    p('hair', hname, len(pts_all), 'strands', 'width', width, 'colour', colour)

# ---------------------------------------------------------------- lights
light_lines = []
for inst in dg.object_instances:
    ob = inst.object
    if ob.type != 'LIGHT' or ob.original.hide_render: continue
    L = ob.data
    M = inst.matrix_world
    pos = swp(M.translation)
    direc = swp((M.to_3x3() @ Vector((0, 0, -1))).normalized())
    col = lin3(L.color)
    if L.type in ('POINT', 'SPOT'):
        I = tuple(c * L.energy / (4 * math.pi) for c in col)
        if L.type == 'POINT':
            # a sphere light lights what is outside it as a point at its centre does; the meshes inside it (the campfire's flames) must not shadow it:
            # in an Eevee scene the emitters are in a group the point lights' shadows pass through
            through = ' "string shadowsfrom" [ "rest" ]' if EEVEE and emitters else ''
            light_lines.append(f'AttributeBegin\n  LightSource "point" "point3 from" [ {f3(pos)} ] "rgb I" [ {f3(I)} ]{through}\nAttributeEnd')
        else:
            half = math.degrees(L.spot_size) / 2
            light_lines.append(f'AttributeBegin\n  LightSource "spot" "point3 from" [ {f3(pos)} ] "point3 to" [ {f3(pos + direc)} ] "rgb I" [ {f3(I)} ] '
                               f'"float coneangle" {half:.4g} "float conedelta" {max(0.01, half * L.spot_blend):.4g}\nAttributeEnd')
    elif L.type == 'AREA':
        sx = L.size; sy = L.size_y if L.shape in ('RECTANGLE', 'ELLIPSE') else L.size
        area = sx * sy * (math.pi / 4 if L.shape in ('DISK', 'ELLIPSE') else 1)
        s3 = M.to_3x3(); area *= abs(s3.col[0].length * s3.col[1].length)
        # Blender hides lights from the camera, a pbrt area light is a visible panel: a wide soft spot with the panel's on-axis intensity (L A = P / pi)
        # stands in for it (no cosine lobe beyond the cone, but the panel faces the stage)
        I = tuple(c * L.energy / math.pi for c in col)
        light_lines.append(f'AttributeBegin\n  LightSource "spot" "point3 from" [ {f3(pos)} ] "point3 to" [ {f3(pos + direc)} ] "rgb I" [ {f3(I)} ] '
                           f'"float coneangle" 85 "float conedelta" 75\nAttributeEnd')
    elif L.type == 'SUN':
        light_lines.append(f'LightSource "distant" "point3 from" [0 0 0] "point3 to" [ {f3(direc)} ] "rgb L" [ {f3(tuple(c * L.energy for c in col))} ]')
    p('light', ob.name, L.type, L.energy, col)

world_col = (0.05, 0.05, 0.05)
w = sc.world
if w and w.use_nodes:
    bg = next((n for n in w.node_tree.nodes if n.bl_idname == 'ShaderNodeBackground'), None)
    if bg: world_col = tuple(c * bg.inputs['Strength'].default_value for c in lin3(bg.inputs['Color'].default_value))
p('world', world_col)

# ---------------------------------------------------------------- camera
cam = sc.camera
cd = cam.data
Mc = cam.matrix_world
eye = swp(Mc.translation)
fwd = swp((Mc.to_3x3() @ Vector((0, 0, -1))).normalized())
up = swp((Mc.to_3x3() @ Vector((0, 1, 0))).normalized())
rx, ry = sc.render.resolution_x, sc.render.resolution_y
sensor = cd.sensor_width if cd.sensor_fit in ('AUTO', 'HORIZONTAL') else cd.sensor_height
if cd.sensor_fit == 'VERTICAL':
    vfov = 2 * math.degrees(math.atan(cd.sensor_height / 2 / cd.lens))
else:
    hsize = sensor / 2 if rx >= ry else sensor / 2 * rx / ry
    hf = math.atan(sensor / 2 / cd.lens)
    vfov = 2 * math.degrees(math.atan(math.tan(hf) * min(rx, ry) / max(rx, ry))) if rx >= ry else 2 * math.degrees(hf)
cam_lines = [f'LookAt {f3(eye)}  {f3(eye + fwd)}  {f3(up)}']
dof = ''
if cd.dof.use_dof:
    fd = (swp(cd.dof.focus_object.matrix_world.translation) - eye).dot(fwd) if cd.dof.focus_object else cd.dof.focus_distance
    lens_m = cd.lens / 1000.0 / sc.unit_settings.scale_length
    lr = lens_m / (2 * cd.dof.aperture_fstop)
    dof = f' "float lensradius" {lr:.5g} "float focaldistance" {fd:.5g}'
    p('dof focus', fd, 'lens radius', lr)
cam_lines.append(f'Camera "perspective" "float fov" {vfov:.5g}{dof}')
p('camera', tuple(eye), 'fov', vfov)

print('STEP writing the scene', flush=True)
TONEMAP = {'Filmic': 'filmic', 'AgX': 'agx', 'Standard': 'none', 'Khronos PBR Neutral': 'neutral'}.get(sc.view_settings.view_transform)
with open(os.path.join(OUT, NAME + '.pbrt'), 'w', encoding='utf-8') as fh:
    fh.write(f'# Converted from {os.path.basename(bpy.data.filepath)} by tools/blender_import/blend_to_pbrt.py\n# @rt-name {NAME}\n')
    if TONEMAP: fh.write(f'# @rt-look tonemap {TONEMAP}\n')
    fh.write('\n'.join(cam_lines) + '\n')
    fh.write(f'Film "rgb" "integer xresolution" {rx} "integer yresolution" {ry} "string filename" "render.exr"\n')
    fh.write('Sampler "zsobol" "integer pixelsamples" 64\nIntegrator "volpath" "integer maxdepth" 8\n')
    fh.write('WorldBegin\n')
    fh.write(f'LightSource "infinite" "rgb L" [ {f3(world_col)} ]\n')
    fh.write('\n'.join(light_lines) + '\n')
    fh.write('\n'.join(lines_world) + '\n')
    fh.write('\n'.join(obj_defs) + '\n')
    fh.write('\n'.join(shape_lines) + '\n')
    fh.write('\n'.join(curve_lines) + '\n')
p('done')
print('STEP done', flush=True)
if missing_images: print('MISSING ' + ', '.join(missing_images), flush=True)
log.close()
