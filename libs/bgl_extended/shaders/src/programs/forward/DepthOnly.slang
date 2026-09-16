import lib.forward.common;

// The depth-only pixel stage: rasterizes for the depth attachment alone, so a pipeline that pairs
// it with a geometry stage writes no colour. A pipeline with no pixel shader at all is
// reflection-only on Metal and cannot draw.
[shader("pixel")]
void PSMain(ForwardVSOut input) {}
