// THIS IS A FILE GENERATED FROM RecordHeader.slang. DO NOT EDIT MANUALLY

// What every record in a raw arena starts with, so a reference names something that says what it
// is. A pass compiled for one kind skips it and reads the payload at cRawPayloadOffset; only a pass
// shared across kinds reads the tag.
public struct RecordHeader
{
    // The arena's own tag enum, as a uint: one header serves every arena and their enums differ, so
    // the enum lives at the edges -- the CPU arena is templated on it, and a shader converts at the
    // one branch that looks at it.
    public uint type;
};
