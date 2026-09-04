// A value that moves from `from` to `to` over `[start, end)` in the render clock: `from` before
// `start`, `to` from `end`, linear between. An end at or before its start is a step at the start.
//
// Holds what to evaluate and never the evaluated value, so a read at any clock is a pure function
// of the record -- which is what keeps a pose at prevTime exact after a write, provided the write
// starts its ramps at or after now.
public struct Ramp
{
    public float from;
    public float to;
    public float start;
    public float end;
};
