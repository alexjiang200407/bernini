#ifndef INDIRECT_DRAW_H
#define INDIRECT_DRAW_H

struct MeshletDispatchArg
{
    uint threadGroupCountX;
    uint threadGroupCountY;
    uint threadGroupCountZ;
};

struct DrawInstance
{
    uint64_t sortKey;
    uint dataIdx;
};

#endif
