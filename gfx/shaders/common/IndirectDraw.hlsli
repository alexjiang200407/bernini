#ifndef INDIRECT_DRAW_H
#define INDIRECT_DRAW_H

struct MeshletIndirectDrawArg
{
    uint threadGroupCountX;
    uint threadGroupCountY;
    uint threadGroupCountZ;
    uint visibleBufferOffset;
};

#endif
