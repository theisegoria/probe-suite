"""Convert a GLB to a plain triangulated OBJ, using Blender in the background.

Only needed if you want to replace the shipped jeep with a mesh of your own.
The repository itself has no Blender dependency: assets/jeep.obj is checked in.

  blender --background --python harness/glb_to_obj.py -- in.glb out.obj [--max-tris N]

Materials, UVs and normals are dropped on purpose. The jeep is a shape the
renderer draws, not a textured asset, and a material pointing at a texture
file the repository does not ship would only produce a broken reference.
"""

import sys

import bpy


def argv():
    a = sys.argv
    return a[a.index("--") + 1:] if "--" in a else []


def main():
    a = argv()
    if len(a) < 2:
        raise SystemExit("usage: ... -- in.glb out.obj [--max-tris N]")
    src, dst = a[0], a[1]
    max_tris = 0
    if "--max-tris" in a:
        max_tris = int(a[a.index("--max-tris") + 1])

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise SystemExit("no mesh in %s" % src)
    for o in meshes:
        o.data.materials.clear()

    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    print("imported %d triangles" % tris)

    if max_tris and tris > max_tris:
        m = obj.modifiers.new("decimate", "DECIMATE")
        m.ratio = max_tris / float(tris)
        bpy.ops.object.modifier_apply(modifier=m.name)
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        print("decimated to %d triangles" % tris)

    bpy.ops.wm.obj_export(filepath=dst, export_selected_objects=False,
                          export_materials=False, export_uv=False,
                          export_normals=False, export_triangulated_mesh=True,
                          forward_axis="NEGATIVE_Z", up_axis="Y")
    print("wrote %s" % dst)


main()
