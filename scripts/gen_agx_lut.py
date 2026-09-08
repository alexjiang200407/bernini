#!/usr/bin/env python3
"""Convert Blender's AgX 3D LUT into the strip texture the tone map samples.

Blender's AgX Base sRGB view is a 57^3 LUT (`AgX_Base_sRGB.cube`) applied in FilmLight E-Gamut
log2 space; `lib/math/Tonemap.slang` reproduces the encoding around it and samples this file.
It is a 2D strip -- slice b at x offset b * size, texel (r, g) inside it -- because neither
backend's texture upload fills a 3D texture yet (docs/plans/agx-lut.md).

    python scripts/gen_agx_lut.py \\
        /Applications/Blender.app/Contents/Resources/5.2/datafiles/colormanagement/luts/AgX_Base_sRGB.cube

Needs numpy, which Blender's Python has and `just init` does not install.

Writes libs/bgl_extended/shaders/src/luts/agx_base_srgb.bin: a 16-byte header (`BLUT`, version 1,
size, 0) and size^3 RGBA16F texels in strip order, alpha 1. Re-run when the Blender reference
changes, and re-measure `AgxCalibration_test`'s sweep with it.
"""

import argparse
import os
import struct
import sys

import numpy as np

MAGIC = b"BLUT"
VERSION = 1
DEFAULT_OUT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..",
    "libs",
    "bgl_extended",
    "shaders",
    "src",
    "luts",
    "agx_base_srgb.bin",
)


def read_cube(path):
    size = None
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if not parts or parts[0].startswith("#"):
                continue
            if parts[0] == "LUT_3D_SIZE":
                size = int(parts[1])
            elif parts[0] == "LUT_1D_SIZE":
                sys.exit("a 1D LUT is not what the tone map samples")
            elif parts[0] in ("DOMAIN_MIN", "DOMAIN_MAX"):
                expected = 0.0 if parts[0] == "DOMAIN_MIN" else 1.0
                if any(float(v) != expected for v in parts[1:4]):
                    sys.exit("the strip is read over [0, 1]; a .cube with another domain is not it")
            elif parts[0] == "TITLE":
                pass
            elif len(parts) == 3:
                rows.append([float(v) for v in parts])
    if size is None or len(rows) != size**3:
        sys.exit("malformed .cube: size %s, %d rows" % (size, len(rows)))
    # .cube order: red fastest, then green, then blue.
    return size, np.array(rows, dtype=np.float32).reshape(size, size, size, 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cube", help="Blender's AgX_Base_sRGB.cube")
    parser.add_argument("--out", default=os.path.normpath(DEFAULT_OUT))
    args = parser.parse_args()

    size, lut = read_cube(args.cube)  # indexed [b, g, r]
    strip = np.ones((size, size * size, 4), dtype=np.float16)
    for b in range(size):
        strip[:, b * size : (b + 1) * size, :3] = lut[b].astype(np.float16)  # [g, r]

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(struct.pack("<4sIII", MAGIC, VERSION, size, 0))
        f.write(strip.tobytes(order="C"))
    print("wrote %s: %d^3 texels, %d bytes" % (args.out, size, os.path.getsize(args.out)))


if __name__ == "__main__":
    main()
