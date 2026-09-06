#!/usr/bin/env python3
"""Render the parity probe in Blender: a matte grey sphere under Blender's own forest.exr.

The `[parity]` case in bgl_extended_tests renders the same sphere from the same camera under the
shipped `forest` environment and compares display luma over the same boxes. The numbers it checks
against come from this script, so a change to the reference is re-measured here and copied there.

Not part of any suite: CI has no Blender, and a gate that passes wherever Blender is absent proves
nothing. Run it by hand:

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
        --python scripts/blender_probe.py -- --out probe.png [--engine CYCLES]

Eevee by default, which is what the Material Preview is. Cycles renders the same scene as the
exact integral of the source, which is what the test's level is asserted against; run both and say
which produced which number.

It also integrates the source itself: the cosine-weighted irradiance at the normal under each
sphere box, in Bernini's longitude convention and the 1/pi convention of its irradiance map, which
is what the test's level is asserted against -- Cycles renders that integral to a percent, and the
float cube the bake convolved is not shipped.

What is measured is exposure and image-based lighting alone. Eevee's shadows and the sun it
extracts from the world above a threshold are switched off: with them on, the sphere carries a
shadow term Bernini has no pass for, and that gap is a lighting feature rather than an asset fix.
Everything else is factory: AgX, look None, exposure 0, the world at strength 1.0.

The camera sits where the test's does -- Bernini's (0, 0, 20) looking at the origin with +Y up,
which in Blender's Z-up frame is +X looking along -X with +Z up. The two renderers disagree about
which way longitude runs on an equirectangular source, so the frames come out mirrored: the boxes
below are placed symmetrically and the test compares each against its mirror.
"""

import argparse
import json
import math
import os
import sys

import bpy
import numpy as np

WIDTH, HEIGHT = 400, 300

# Sphere limbs, where the normal is closest to the horizontal axis, and two backdrop corners.
# Symmetric about the frame's centre line, so a mirrored frame maps each box onto its opposite.
BOX = 16
BOXES = {
    "sphereLeft": (141, 142),
    "sphereRight": (243, 142),
    "skyLeft": (10, 20),
    "skyRight": (374, 20),
}


# The normal under the centre of each sphere box: radius 5 at the origin, seen from 20 away through
# a 60 degree vertical field, the box centre 51 px off the frame's centre line.
NORMALS = {
    "sphereLeft": (-0.632, 0.0, 0.775),
    "sphereRight": (0.632, 0.0, 0.775),
}


def irradiance(hdr):
    """Cosine-weighted irradiance of the equirectangular source at each normal, as Rec.709 luma.

    Bernini reads longitude as `u = 0.5 + atan2(x, z) / 2pi` and latitude as `v = acos(y) / pi`,
    and its irradiance map divides the integral by pi, so a Lambertian surface reflects
    albedo * irradiance. Both conventions are reproduced here rather than Blender's.
    """
    img = bpy.data.images.load(hdr)
    w, h = img.size
    px = np.array(img.pixels[:], dtype=np.float64).reshape(h, w, 4)[::-1, :, :3]
    luma = 0.2126 * px[..., 0] + 0.7152 * px[..., 1] + 0.0722 * px[..., 2]
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    lon = (u - 0.5) * 2.0 * math.pi
    theta = v * math.pi
    sin_theta = np.sin(theta)[:, None]
    dx = np.sin(lon)[None, :] * sin_theta
    dy = np.cos(theta)[:, None] * np.ones((1, w))
    dz = np.cos(lon)[None, :] * sin_theta
    solid_angle = (2.0 * math.pi / w) * (math.pi / h) * sin_theta
    out = {}
    for name, (nx, ny, nz) in NORMALS.items():
        cos = np.clip(nx * dx + ny * dy + nz * dz, 0.0, None)
        out[name] = round(float((luma * cos * solid_angle).sum() / math.pi), 4)
    return out


def forest_exr():
    return os.path.join(
        bpy.utils.system_resource("DATAFILES"), "studiolights", "world", "forest.exr"
    )


def build_scene(hdr, samples, engine):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE" if engine == "EEVEE" else "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    scene.cycles.use_adaptive_sampling = False
    scene.render.resolution_x = WIDTH
    scene.render.resolution_y = HEIGHT
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.eevee.taa_render_samples = samples
    scene.eevee.use_shadows = False
    scene.eevee.use_raytracing = False

    assert scene.view_settings.view_transform == "AgX", scene.view_settings.view_transform
    assert scene.view_settings.look == "None", scene.view_settings.look
    assert scene.view_settings.exposure == 0.0

    for obj in list(scene.objects):
        bpy.data.objects.remove(obj, do_unlink=True)

    world = scene.world
    world.use_nodes = True
    world.sun_threshold = 0.0  # zero disables the extraction; any other value is a threshold
    world.use_sun_shadow = False
    nodes = world.node_tree.nodes
    links = world.node_tree.links
    nodes.clear()
    env = nodes.new("ShaderNodeTexEnvironment")
    env.image = bpy.data.images.load(hdr)
    background = nodes.new("ShaderNodeBackground")
    background.inputs["Strength"].default_value = 1.0
    out = nodes.new("ShaderNodeOutputWorld")
    links.new(env.outputs["Color"], background.inputs["Color"])
    links.new(background.outputs["Background"], out.inputs["Surface"])

    bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, radius=5.0)
    sphere = bpy.context.active_object
    bpy.ops.object.shade_smooth()
    mat = bpy.data.materials.new("probe")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.18, 0.18, 0.18, 1.0)
    bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Roughness"].default_value = 1.0
    bsdf.inputs["Specular IOR Level"].default_value = 0.0  # Lambertian: the two split-sum models differ by construction
    sphere.data.materials.append(mat)

    cam_data = bpy.data.cameras.new("probe")
    cam_data.sensor_fit = "VERTICAL"
    cam_data.lens_unit = "FOV"
    cam_data.angle_y = math.radians(60.0)
    cam_data.clip_start = 0.5
    cam_data.clip_end = 500.0
    cam = bpy.data.objects.new("probe", cam_data)
    cam.location = (20.0, 0.0, 0.0)
    cam.rotation_euler = (math.radians(90.0), 0.0, math.radians(90.0))
    scene.collection.objects.link(cam)
    scene.camera = cam


def render(out):
    scene = bpy.context.scene
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.image_settings.color_depth = "8"
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)


def box_means(png):
    """Mean display RGB and Rec.709 luma of each box, read back as the bytes the file holds."""
    img = bpy.data.images.load(png)
    img.colorspace_settings.name = "Non-Color"
    w, h = img.size
    px = img.pixels[:]
    result = {}
    for name, (x, y) in BOXES.items():
        acc = [0.0, 0.0, 0.0]
        for row in range(y, y + BOX):
            # Blender's pixel rows run bottom to top; the boxes are given top-down like the test's.
            base = ((h - 1 - row) * w + x) * 4
            for col in range(BOX):
                o = base + col * 4
                acc[0] += px[o]
                acc[1] += px[o + 1]
                acc[2] += px[o + 2]
        n = BOX * BOX
        rgb = [c / n for c in acc]
        result[name] = {
            "rgb": [round(c, 4) for c in rgb],
            "luma": round(0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2], 4),
        }
    return result


def main():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="Where the rendered PNG goes")
    parser.add_argument("--hdr", default=forest_exr(), help="Equirectangular source (default: Blender's forest.exr)")
    parser.add_argument("--samples", type=int, default=64)
    parser.add_argument("--engine", choices=("EEVEE", "CYCLES"), default="EEVEE")
    args = parser.parse_args(argv)

    build_scene(args.hdr, args.samples, args.engine)
    render(args.out)
    print(
        json.dumps(
            {
                "blender": bpy.app.version_string,
                "engine": args.engine,
                "boxes": box_means(args.out),
                "irradiance": irradiance(args.hdr),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
