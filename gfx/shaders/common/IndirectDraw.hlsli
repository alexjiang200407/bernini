#ifndef INDIRECT_DRAW_H
#define INDIRECT_DRAW_H

struct MeshletIndirectDrawArg
{
    uint threadGroupCountX;
    uint threadGroupCountY;
    uint threadGroupCountZ;
    uint visibleBufferOffset;
};

struct DrawInstance
{
    uint64_t sortKey;
    uint dataIdx;
};

#endif
