"""Write a project's authored material factors back into the .glb they were imported from.

An import copies the source's material factors once and then lets go: the import document is
authoritative for materials, so everything authored in the editor afterwards lives only in the
`.bmaterial` files. Re-import the source and that authoring is gone. This carries it the other way,
so the copied source describes what the project actually draws and a re-import starts from there.

Only the factors travel, because only they exist on both sides. Routes, baked triplets and material
graphs are the project's own and have no glTF spelling; textures are already the source's, and
fix_cha800_textures.py is the direction that corrects those.

`specularFactor` and `transmissionFactor` need KHR_materials_specular and
KHR_materials_transmission, which are added to `extensionsUsed` when a material actually carries a
non-default one -- both are read back by assetlib's importer.

Bernini's `hashed` alpha mode has no glTF spelling. It imports from MASK, and is reported rather than
written, so a material authored into it is not silently flattened.

Usage: sync_materials_to_glb.py <project data root> <in.glb> <out.glb>
"""
import json
import struct
import sys
from pathlib import Path

JSON_CHUNK = 0x4E4F534A

SPECULAR_EXT = 'KHR_materials_specular'
TRANSMISSION_EXT = 'KHR_materials_transmission'

# glTF's own defaults, which a material at one of them writes nothing for.
DEFAULTS = {
    'baseColorFactor': [1.0, 1.0, 1.0, 1.0],
    'roughnessFactor': 1.0,
    'metallicFactor': 1.0,
    'alphaCutoff': 0.5,
    'specularFactor': 1.0,
    'specularColorFactor': [1.0, 1.0, 1.0],
    'transmissionFactor': 0.0,
}

ALPHA_MODE = {'opaque': 'OPAQUE', 'mask': 'MASK', 'blend': 'BLEND'}


def read_glb(path):
    d = Path(path).read_bytes()
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
    Path(path).write_bytes(struct.pack('<III', 0x46546C67, ver, 12 + len(body)) + body)


def authored(data_root):
    """{material name: parsed .bmaterial} over the project, refusing a name two files claim."""
    out = {}
    for path in sorted(Path(data_root).rglob('*.bmaterial')):
        d = json.loads(path.read_text())
        name = d.get('name')
        if name is None:
            continue
        assert name not in out, f'two materials named {name}: {out[name]["_path"]} and {path}'
        d['_path'] = str(path)
        out[name] = d
    return out


def f32(v):
    """`v` at the precision both sides actually store, so a re-serialized 0.16 that reads back as
    0.1599999964237213 is not mistaken for an edit."""
    one = lambda x: struct.unpack('<f', struct.pack('<f', float(x)))[0]
    return [one(x) for x in v] if isinstance(v, list) else one(v)


def factors(d):
    """The glTF-expressible half of a `.bmaterial`, at glTF's own defaults where it says nothing.

    Unrounded: these are authored slider values (0.22000457, not 0.22), and rounding them here would
    make the round trip lossy in a way nothing downstream could tell from a real edit."""
    return {k: d.get(k, DEFAULTS[k]) for k in DEFAULTS}


def apply(mat, name, want, report):
    """Writes `want` onto glb material `mat`. Returns the fields that changed."""
    pbr = mat.setdefault('pbrMetallicRoughness', {})
    ext = mat.setdefault('extensions', {})
    changed = {}

    def put(holder, key, value, used_ext=None):
        was = holder.get(key, DEFAULTS[key])
        if f32(was) == f32(value):
            return
        changed[key] = (was, value)
        if value == DEFAULTS[key]:
            holder.pop(key, None)  # a default is glTF's already; saying it adds nothing
        else:
            holder[key] = value
            if used_ext:
                report.setdefault('extensions', set()).add(used_ext)

    put(pbr, 'baseColorFactor', want['baseColorFactor'])
    put(pbr, 'roughnessFactor', want['roughnessFactor'])
    put(pbr, 'metallicFactor', want['metallicFactor'])

    spec = ext.setdefault(SPECULAR_EXT, {})
    put(spec, 'specularFactor', want['specularFactor'], SPECULAR_EXT)
    put(spec, 'specularColorFactor', want['specularColorFactor'], SPECULAR_EXT)
    if not spec:
        ext.pop(SPECULAR_EXT, None)

    trans = ext.setdefault(TRANSMISSION_EXT, {})
    put(trans, 'transmissionFactor', want['transmissionFactor'], TRANSMISSION_EXT)
    if not trans:
        ext.pop(TRANSMISSION_EXT, None)

    if not ext:
        mat.pop('extensions', None)

    # alphaCutoff only means anything under MASK, which is also the only mode that writes one.
    if mat.get('alphaMode') == 'MASK':
        put(mat, 'alphaCutoff', want['alphaCutoff'])
    return changed


def sync(data_root, in_path, out_path):
    ver, chunks = read_glb(in_path)
    j = json.loads(chunks[0][1].decode('utf-8'))
    by_name = authored(data_root)

    report = {}
    lines = []
    notes = []
    missing = []
    for mat in j['materials']:
        name = mat.get('name')
        d = by_name.get(name)
        if d is None:
            missing.append(name)
            continue

        mode = d.get('alphaMode', 'opaque')
        if mode not in ALPHA_MODE:
            notes.append(f'  {name}: alphaMode {mode!r} has no glTF spelling, left as '
                         f'{mat.get("alphaMode", "OPAQUE")}')
        elif ALPHA_MODE[mode] != mat.get('alphaMode', 'OPAQUE'):
            lines.append(f'  {name}: alphaMode {mat.get("alphaMode", "OPAQUE")} -> '
                         f'{ALPHA_MODE[mode]}')
            if ALPHA_MODE[mode] == 'OPAQUE':
                mat.pop('alphaMode', None)
            else:
                mat['alphaMode'] = ALPHA_MODE[mode]

        for key, (was, now) in apply(mat, name, factors(d), report).items():
            lines.append(f'  {name}: {key} {was} -> {now}')

    for used in sorted(report.get('extensions', ())):
        if used not in j.setdefault('extensionsUsed', []):
            j['extensionsUsed'].append(used)
            lines.append(f'  extensionsUsed += {used}')
    if not j.get('extensionsUsed'):
        j.pop('extensionsUsed', None)

    chunks[0][1] = json.dumps(j, separators=(',', ':')).encode('utf-8')
    write_glb(out_path, ver, chunks)
    return lines, notes, missing


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    changes, notes, absent = sync(sys.argv[1], sys.argv[2], sys.argv[3])
    for line in changes + notes:
        print(line)
    if absent:
        print(f'  {len(absent)} glb material(s) the project does not author: {absent}')
    print(f'{len(changes)} change(s); wrote {sys.argv[3]}')
