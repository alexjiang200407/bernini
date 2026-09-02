import idl.IPlayback;
import idl.MeshInstance;
import idl.Meshlet;
import idl.PlaybackType;
import idl.RawEntry;
import idl.Submesh;
import lib.types.EntryBuffer;
import lib.types.PackedBuffer;
import lib.types.RangeBuffer;
import lib.types.RawBuffer;
import lib.data.SubmeshInstance;

public struct ForwardData
{
    public PackedBuffer<SubmeshInstance> instanceBuffer;
    public EntryBuffer<MeshInstance> meshBuffer;
    public RangeBuffer<Submesh> submeshBuffer;
    public RangeBuffer<Meshlet> meshletBuffer;
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
