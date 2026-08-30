import IPlayback;
import RangeWithCount;
import RawEntry;
import Submesh;

public struct Mesh
{
    public float4x4 transform;
    public RangeWithCount<Submesh> submeshes;

    // Null for a static mesh. Otherwise a record in the view's playback arena whose header says
    // which tier it is, so the one stage that draws more than one tier can ask rather than mirror
    // the PSO table.
    public RawEntry<IPlayback> playback;
};
