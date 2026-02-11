#ifndef MATERIAL_H
#define MATERIAL_H

#define GREEN_MATERIAL 1
#define RED_MATERIAL   2

struct Material
{
    uint materialType;
};

struct MaterialTypeData
{
    uint visibleCount;
};

#endif
