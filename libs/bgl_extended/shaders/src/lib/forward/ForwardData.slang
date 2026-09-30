import idl.Geom;
import idl.IPlayback;
import idl.MeshInstance;
import idl.Meshlet;
import idl.MeshletGroup;
import idl.PlaybackType;
import idl.RawEntry;
import idl.Submesh;
import lib.types.EntryBuffer;
import lib.types.PackedBuffer;
import lib.types.RangeBuffer;
import lib.types.RawBuffer;
import idl.IVertexSource;
import lib.data.SubmeshInstance;

// The vertex decode reads through IVertexSource, the renderer's interface; the arena is bgpu's, so
// the conformance is declared where the renderer binds one as its vertex data.
public extension RawBuffer : IVertexSource {}

public struct ForwardData
{
    public PackedBuffer<SubmeshInstance> instanceBuffer;
    public EntryBuffer<MeshInstance> meshBuffer;
    public EntryBuffer<Geom> geomBuffer;
    public RangeBuffer<Submesh> submeshBuffer;
    public RangeBuffer<Meshlet> meshletBuffer;
    public RangeBuffer<MeshletGroup> meshletGroupBuffer;
    public RangeBuffer<uint> vertexMapBuffer;
    public RawBuffer vertexDataBuffer;
    public RangeBuffer<uint> indexBuffer;

    // Every animated placement's playback record, each behind a header naming its tier. A static
    // placement owns none and its MeshInstance.playback is null.
    public RawBuffer playbackBuffer;

    // Which tier a placement's record is. Only the stage that draws more than one tier asks; the
    // rest are compiled for a single one and load the payload straight.
    //
    // @pre entry is non-null -- a static placement has no record and no header to read.
    public PlaybackType GetPlaybackKind(RawEntry<IPlayback> entry) { return PlaybackType(playbackBuffer.LoadTag(entry)); }
};
