"""Correct the three base-colour maps in cha800_00.glb that render far brighter than what they depict.

The rig imports and draws correctly; three of its maps do not describe what they are attached to.
Each is a property of the source image, measured here rather than hardcoded, so re-running against a
re-exported glb re-derives the same correction:

  lash_bc    a flat brown swatch -- its whole range is R 65..131, with no dark value anywhere -- read
             by both the eyelash and the eyebrow strands. Ten times the hair's linear value, so
             lashes and brows render pale khaki against black hair. Rescaled to the hair's own value.

  hair_bc    one tile of the strand atlas is neutral grey ~164 on a grey ~128 backing where every
             other tile is ~31 on black. Only the flyaway/sideburn cards sample it, so only they come
             out white. The tile is rescaled to the atlas's own strand value, and its backing zeroed
             so no coarse mip can bleed grey into a strand.

  eyewet_bc  carries the same mask twice: RGB, and its complement in alpha. Coverage and colour are
             therefore perfectly anti-correlated -- the only band that ever shows is the mid-alpha
             one, and there the colour is bright grey. The mask is kept in alpha and the colour
             driven to near-black, leaving the wet layer to read as a specular glint.

Applying the result. Where an import document records the folder its textures went to, replacing
meshes_src/cha800_00.glb and running `assetlib_cli migrate -p <project> -y` re-extracts them and
re-saves the mesh, leaving only the two by-hand steps at the end. cha800_00's document predates that
field, records no folder, and so is never reported stale (AssetStore::StaleImportedTextureSources) --
its textures have to be regenerated the long way:

  1. fix_cha800_textures.py resources/cha800_00.glb fixed.glb
  2. move Meshes/, Materials/, Skeletons/, Animations/cha800_00 and meshes_src/cha800_00.* aside,
     delete textures_src/cha800_00, then `assetlib_cli bake -p <project> -n cha800_00 fixed.glb`
     purely to regenerate textures_src/cha800_00 -- that import refuses to write over an existing
     one, and writes a flat layout with no materials when it does run
  3. restore the four directories and the .bimport, keeping the regenerated textures_src and the
     fixed glb as meshes_src/cha800_00.glb
  4. `assetlib_cli migrate -p <project> -y`, which re-saves the mesh, skeleton and clips against the
     new source stamp

Then, either way:

  5. re-bake the six materials that read the four changed maps -- Eyewet, EyeLash, Blow and the three
     Hair. There is no CLI for it; the editor bakes from the Material Editor or the Content Explorer.
     Until it happens the rig does not merely look stale, it fails to load: AddSkinnedMeshGeom
     refuses a loose material, and a stale bake is what makes one.
  6. material *parameters* are authored in the editor and do not follow the glb after an import --
     the import document is authoritative for materials. sync_materials_to_glb.py carries them the
     other way, so a re-import starts from what the project already draws.
"""
import io
import json
import struct
import sys

from PIL import Image

JSON_CHUNK = 0x4E4F534A

# What the lashes, brows and the odd hair tile are all rescaled to: the value the rest of the strand
# atlas already uses, measured off it. Named so the three corrections cannot drift apart.
HAIR_ATLAS_IMAGE = 'hair_bc'

# A strand texel, as opposed to the mask's soft edge. Well clear of the 0.5 an alpha test would use.
OPAQUE_ALPHA = 200

# What the wet layer's colour becomes, in sRGB. Not zero: a pure black diffuse under partial coverage
# darkens the eye behind it, which trades a white rim for a dark one.
EYE_WET_SRGB = 10



def to_linear(v):
    x = v / 255.0
    return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4


def to_srgb(v):
    x = 12.92 * v if v <= 0.0031308 else 1.055 * (v ** (1 / 2.4)) - 0.055
    return max(0, min(255, round(x * 255.0)))


def read_glb(path):
    d = open(path, 'rb').read()
    magic, ver, _ = struct.unpack_from('<III', d, 0)
    assert magic == 0x46546C67, path
    off, chunks = 12, []
    while off < len(d):
        ln, ty = struct.unpack_from('<II', d, off)
        chunks.append([ty, d[off + 8:off + 8 + ln]])
        off += 8 + ln
    return ver, chunks


def write_glb(path, ver, chunks):
    body = b''
    for ty, payload in chunks:
        pad = (-len(payload)) % 4
        # JSON pads with spaces, BIN with zeroes; a parser reads the padded length either way.
        payload = payload + (b' ' if ty == JSON_CHUNK else b'\x00') * pad
        body += struct.pack('<II', len(payload), ty) + payload
    open(path, 'wb').write(struct.pack('<III', 0x46546C67, ver, 12 + len(body)) + body)


def repack(j, bin_data, replacements):
    """A new BIN chunk with `replacements` (bufferView index -> bytes) swapped in, every bufferView
    relaid at a 4-byte boundary and its offset rewritten. Accessors address their bufferView, not the
    buffer, so moving a view whole leaves them valid -- and 4 covers every glTF component size."""
    out = bytearray()
    for i, bv in enumerate(j['bufferViews']):
        assert 'buffer' not in bv or bv['buffer'] == 0, 'GLB has one buffer'
        payload = replacements.get(i)
        if payload is None:
            off = bv.get('byteOffset', 0)
            payload = bin_data[off:off + bv['byteLength']]
        out += b'\x00' * ((-len(out)) % 4)
        bv['byteOffset'] = len(out)
        bv['byteLength'] = len(payload)
        out += payload
    out += b'\x00' * ((-len(out)) % 4)
    j['buffers'][0]['byteLength'] = len(out)
    return bytes(out)


def image_index(j, name):
    hits = [i for i, im in enumerate(j['images']) if im.get('name') == name]
    assert len(hits) == 1, f'expected exactly one image named {name}, found {len(hits)}'
    return hits[0]


def load_image(j, bin_data, index):
    bv = j['bufferViews'][j['images'][index]['bufferView']]
    off = bv.get('byteOffset', 0)
    return Image.open(io.BytesIO(bin_data[off:off + bv['byteLength']])).convert('RGBA')


def encode(im, keep_alpha):
    buf = io.BytesIO()
    im.convert('RGBA' if keep_alpha else 'RGB').save(buf, format='PNG', optimize=True)
    return buf.getvalue()


def read_uvs(j, bin_data, accessor):
    a = j['accessors'][accessor]
    bv = j['bufferViews'][a['bufferView']]
    assert a['componentType'] == 5126 and a['type'] == 'VEC2', 'UVs are not float2'
    stride = bv.get('byteStride') or 8
    base = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    return [struct.unpack_from('<ff', bin_data, base + k * stride) for k in range(a['count'])]


def samplers_of(j, image):
    """{material index: material name} for every material whose base colour is `image`."""
    textures = {t for t, tex in enumerate(j['textures']) if tex['source'] == image}
    mats = {
        m: mat.get('name')
        for m, mat in enumerate(j['materials'])
        if mat.get('pbrMetallicRoughness', {}).get('baseColorTexture', {}).get('index') in textures
    }
    return mats


def uv_span(j, bin_data, material):
    lo, hi = 1e9, -1e9
    for mesh in j['meshes']:
        for p in mesh['primitives']:
            if p.get('material') != material:
                continue
            us = [u for u, _ in read_uvs(j, bin_data, p['attributes']['TEXCOORD_0'])]
            lo, hi = min(lo, min(us)), max(hi, max(us))
    return lo, hi


def strand_mean(im, x0, x1):
    """Mean linear RGB of the opaque texels in the column band [x0, x1), and how many there were."""
    px = im.load()
    tot = [0.0, 0.0, 0.0]
    n = 0
    for y in range(im.size[1]):
        for x in range(x0, x1):
            p = px[x, y]
            if p[3] >= OPAQUE_ALPHA:
                for c in range(3):
                    tot[c] += to_linear(p[c])
                n += 1
    assert n, f'no opaque texels in columns [{x0}, {x1})'
    return [t / n for t in tot], n


def scale_to(im, target, current, box=None):
    """`im` with its RGB scaled per channel so `current` linear mean becomes `target`, in `box`."""
    k = [(target[c] / current[c]) if current[c] > 0 else 1.0 for c in range(3)]
    lut = [[to_srgb(to_linear(v) * k[c]) for v in range(256)] for c in range(3)]
    px = im.load()
    x0, y0, x1, y1 = box or (0, 0, im.size[0], im.size[1])
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = px[x, y]
            px[x, y] = (lut[0][p[0]], lut[1][p[1]], lut[2][p[2]], p[3])
    return k


def atlas_copies(j, bin_data, index):
    """Every image byte-identical to `index`. The glb carries the strand atlas twice, and the tile
    at issue is reached through one copy while the boundary that locates it is set on the other."""
    def raw(i):
        bv = j['bufferViews'][j['images'][i]['bufferView']]
        off = bv.get('byteOffset', 0)
        return bin_data[off:off + bv['byteLength']]

    want = raw(index)
    return [i for i in range(len(j['images'])) if 'bufferView' in j['images'][i] and raw(i) == want]


def fix_hair_tile(j, bin_data, replacements, atlas_linear, report):
    """Rescale the one atlas tile no other material samples, and zero its non-black backing."""
    copies = atlas_copies(j, bin_data, image_index(j, HAIR_ATLAS_IMAGE))
    spans = {}
    for index in copies:
        for m, mat in samplers_of(j, index).items():
            if mat:
                spans[mat] = uv_span(j, bin_data, m)
    assert len(spans) > 1, f'the strand atlas is read by {list(spans)} alone'

    # The tile to correct is the one a single material has to itself, beyond every other's reach.
    # Derived rather than hardcoded, so a re-export that re-packs the atlas still lands on it.
    outliers = [m for m, (lo, _) in spans.items() if all(lo > hi for o, (_, hi) in spans.items() if o != m)]
    assert len(outliers) == 1, f'expected one isolated tile, found {outliers}'
    boundary = max(hi for m, (_, hi) in spans.items() if m != outliers[0])

    for index in copies:
        im = load_image(j, bin_data, index)
        x0 = int(boundary * im.size[0]) + 1
        before, n = strand_mean(im, x0, im.size[0])
        scale_to(im, atlas_linear, before, box=(x0, 0, im.size[0], im.size[1]))

        # The backing under the tile's zero-coverage texels is grey where the atlas's is black. Alpha
        # already hides it, but a coarse mip averages colour and coverage separately and it bleeds.
        px = im.load()
        for y in range(im.size[1]):
            for x in range(x0, im.size[0]):
                if px[x, y][3] == 0:
                    px[x, y] = (0, 0, 0, 0)

        replacements[j['images'][index]['bufferView']] = encode(im, keep_alpha=True)
        after, _ = strand_mean(im, x0, im.size[0])
        report.append(
            f'  {j["images"][index].get("name"):14} tile {outliers[0]} owns x>={x0} '
            f'({n} strand texels): sRGB {tuple(to_srgb(c) for c in before)} '
            f'-> {tuple(to_srgb(c) for c in after)}')


def fix(in_path, out_path):
    ver, chunks = read_glb(in_path)
    j = json.loads(chunks[0][1].decode('utf-8'))
    bin_data = chunks[1][1]
    replacements, report = {}, []

    # Everything else is measured against the strand atlas, so read its value before touching it.
    atlas = load_image(j, bin_data, image_index(j, HAIR_ATLAS_IMAGE))
    atlas_linear, _ = strand_mean(atlas, 0, atlas.size[0] // 2)
    report.append(f'  hair strand value: sRGB {tuple(to_srgb(c) for c in atlas_linear)}')

    lash = image_index(j, 'lash_bc')
    im = load_image(j, bin_data, lash)
    before, n = strand_mean(im, 0, im.size[0])
    scale_to(im, atlas_linear, before)
    after, _ = strand_mean(im, 0, im.size[0])
    replacements[j['images'][lash]['bufferView']] = encode(im, keep_alpha=False)
    report.append(
        f'  lash_bc        {n} texels: sRGB {tuple(to_srgb(c) for c in before)} '
        f'-> {tuple(to_srgb(c) for c in after)}')

    fix_hair_tile(j, bin_data, replacements, atlas_linear, report)

    wet = image_index(j, 'eyewet_bc')
    im = load_image(j, bin_data, wet)
    px = im.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            px[x, y] = (EYE_WET_SRGB, EYE_WET_SRGB, EYE_WET_SRGB, px[x, y][3])
    replacements[j['images'][wet]['bufferView']] = encode(im, keep_alpha=True)
    report.append(f'  eyewet_bc      colour -> sRGB {EYE_WET_SRGB}, mask kept in alpha')

    chunks[1][1] = repack(j, bin_data, replacements)
    chunks[0][1] = json.dumps(j, separators=(',', ':')).encode('utf-8')
    write_glb(out_path, ver, chunks)
    return report


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    for line in fix(sys.argv[1], sys.argv[2]):
        print(line)
    print(f'wrote {sys.argv[2]}')
