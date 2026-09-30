"""What `bgpu_idlgen --public` accepts into a committed header, and what it refuses.

A public header is generated on whichever backend built last and committed, so a struct in one must
come out byte-for-byte the same with and without `--metal-layout`. These run the built tool, and skip
when this checkout has not built it.
"""

import os
import subprocess

import pytest

import gen_idl
import util.config as cfg


@pytest.fixture(scope="module")
def idlgen():
    try:
        tool = gen_idl.resolve_tool(cfg.build_dir(None), cfg.artifact_config(None))
    except Exception as err:  # no configured build dir at all
        pytest.skip(f"no build to find bgpu_idlgen in: {err}")
    if not tool or not os.path.isfile(tool):
        pytest.skip("bgpu_idlgen is not built (`just build bgpu_idlgen`)")
    return tool


def generate(tool, tmp_path, source, *flags):
    """(exit code, stderr, header text or None) for one module generated as a public header."""
    src = tmp_path / "src"
    out = tmp_path / ("out-" + "-".join(f.strip("-") for f in flags))
    src.mkdir(exist_ok=True)
    module = src / "Probe.slang"
    module.write_text(source)
    run = subprocess.run(
        [tool, "--src-root", str(src), "--cpp-out-dir", str(out), "--namespace", "bgl",
         "--public", *flags, str(module)],
        cwd=os.path.dirname(tool), capture_output=True, text=True)
    header = out / "Probe.h"
    return run.returncode, run.stderr, header.read_text() if header.is_file() else None


AGREES = """
public struct Probe
{
    public float2 position;
    public float2 uv;
    public uint color;
    public uint reserved;
};
"""

# 12 bytes to the C/C++ rules; MSL aligns the float2 to 8 and rounds the struct up to 16.
NEEDS_ALIGNAS = """
public struct Probe
{
    public float2 position;
    public uint color;
};
"""

# The handle sits at 4 in the scalar layout D3D12 reads and at 8 under MSL.
OFFSETS_DISAGREE = """
public struct Probe
{
    public uint a;
    public Texture2D.Handle texture;
    public uint b;
};
"""


def test_a_struct_every_backend_agrees_on_generates_the_same_header_on_metal(idlgen, tmp_path):
    rc, err, metal = generate(idlgen, tmp_path, AGREES, "--metal-layout")
    assert rc == 0, err
    rc, err, scalar = generate(idlgen, tmp_path, AGREES)
    assert rc == 0, err
    assert metal == scalar
    assert "alignas" not in metal
    assert "static_assert(sizeof(Probe) == 24);" in metal


@pytest.mark.parametrize("flags", [(), ("--metal-layout",)])
def test_a_struct_only_an_alignas_would_size_correctly_is_refused(idlgen, tmp_path, flags):
    rc, err, header = generate(idlgen, tmp_path, NEEDS_ALIGNAS, *flags)
    assert rc != 0
    assert header is None
    assert "16 bytes under MSL" in err and "12 under the C/C++ rules" in err
    assert "alignas" in err


@pytest.mark.parametrize("flags", [(), ("--metal-layout",)])
def test_a_struct_whose_field_offsets_disagree_is_refused(idlgen, tmp_path, flags):
    rc, err, header = generate(idlgen, tmp_path, OFFSETS_DISAGREE, *flags)
    assert rc != 0
    assert header is None
    assert "'Probe::texture' sits at 8 under MSL but 4 under the C/C++ rules" in err
    assert "cannot go in a public header" in err
