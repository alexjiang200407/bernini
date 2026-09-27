import idl.Geom;
import idl.InstanceLod;
import lib.types.ComputeBuffer;

// Packs a few levels of detail the way the cull will, and one geom's submesh entry lookup, for a
// CPU comparison: whether InstanceLod::Make and bgl::UnpackInstanceLod agree on the bits, and
// whether Geom::SubmeshEntry lands where the upload put a level's submesh.
struct Uniforms
{
    ComputeBuffer<InstanceLod> words;
    ComputeBuffer<uint> entries;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    gUniforms.words[0] = InstanceLod.Make(0u, false, 0u, 1.0);
    gUniforms.words[1] = InstanceLod.Make(2u, false, 5u, 0.5);
    gUniforms.words[2] = InstanceLod.Make(1u, true, 0u, 0.5);
    gUniforms.words[3] = InstanceLod.Make(3u, true, 1u, 0.0);
    gUniforms.words[4] = InstanceLod.Make(7u, true, 6u, 1.0);

    Geom geom;
    geom.submeshCount = 3u;
    geom.lodCount = 2u;
    gUniforms.entries[0] = geom.SubmeshEntry(0u, 0u);
    gUniforms.entries[1] = geom.SubmeshEntry(0u, 2u);
    gUniforms.entries[2] = geom.SubmeshEntry(1u, 0u);
    gUniforms.entries[3] = geom.SubmeshEntry(1u, 2u);
}
