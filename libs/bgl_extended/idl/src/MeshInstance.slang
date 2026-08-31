import IPlayback;
import RangeWithCount;
import RawEntry;
import Submesh;

// One placement of a geom: its world transform, the geom's submesh range copied in by value, and
// its playback record. The geom itself has no GPU record -- see
// docs/specs/gpu_scene_instance_split.md.
public struct MeshInstance
{
    // The three rows of an affine placement; the fourth is always (0,0,0,1) and is not stored, as a
    // bone's palette entry is not. Rows and not columns, which a transposed read cannot tell apart
    // under an identity transform -- so go through the accessors below, and fill it with
    // bgl::WriteInstanceTransform.
    public float4 transform[3];

    public RangeWithCount<Submesh> submeshes;

    // Null for a static mesh. Otherwise a record in the view's playback arena whose header says
    // which tier it is, so the one stage that draws more than one tier can ask rather than mirror
    // the PSO table.
    public RawEntry<IPlayback> playback;

    public float3 TransformPoint(float3 p)
    {
        let h = float4(p, 1.0);
        return float3(dot(transform[0], h), dot(transform[1], h), dot(transform[2], h));
    }

    // The translation is dropped rather than added, which is what makes this a direction. Not the
    // inverse transpose, so a normal under non-uniform scale comes out skewed -- placements are TRS
    // and the only scale variation planned is uniform.
    public float3 TransformDirection(float3 d)
    {
        return float3(dot(transform[0].xyz, d), dot(transform[1].xyz, d), dot(transform[2].xyz, d));
    }
};
