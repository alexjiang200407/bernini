// THIS IS A FILE GENERATED FROM DebugRecord.slang. DO NOT EDIT MANUALLY

public struct DebugRecord
{
    public uint errcode;

    // What the shader computed, and the bound it had to be under. An assertion that is not a
    // comparison leaves both zero.
    public uint value;
    public uint limit;

    // Where it happened -- the submesh, meshlet or instance the raising code was working on. Which
    // of those it names is the errcode's business.
    public uint context;
}
