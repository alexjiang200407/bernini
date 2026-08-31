// THIS IS A FILE GENERATED FROM MeshInstance.slang. DO NOT EDIT MANUALLY

import IPlayback;
import RangeWithCount;
import RawEntry;
import Submesh;

// One placement of a geom: its world transform, the geom's submesh range copied in by value, and
// its playback record. The geom itself has no GPU record -- see
// docs/specs/gpu_scene_instance_split.md.
public struct MeshInstance
{
    public float4x4 transform;
    public RangeWithCount<Submesh> submeshes;

    // Null for a static mesh. Otherwise a record in the view's playback arena whose header says
    // which tier it is, so the one stage that draws more than one tier can ask rather than mirror
    // the PSO table.
    public RawEntry<IPlayback> playback;
};
