/** Where one tick's records sit in the render ring; a count of 0 for a tick the ring does not hold. */
public struct TickRecords
{
    public uint first;
    public uint count;
};

// CrowdInstanceWriter::Params::lastFrame: where last frame's pose of an agent is found.
public static const uint c_LastFrameSameTick = 0u;
public static const uint c_LastFrameTickBefore = 1u;
public static const uint c_LastFrameNone = 2u;
