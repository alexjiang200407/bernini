#ifndef SORT_KEY_HLSLI
#define SORT_KEY_HLSLI

#define MAX_PSO_BINS 3 

uint GetLayer(uint64_t sortKey)
{
    return uint((sortKey >> 62) & 0x3);
}

// Extract PSO index from sortKey (bits 48-61, 14 bits)
uint GetPSOIndex(uint64_t sortKey)
{
    return uint((sortKey >> 48) & 0x3FFF);
}

// Extract material type (bits 40-47) - for info only
uint GetMaterialType(uint64_t sortKey)
{
    return uint((sortKey >> 40) & 0xFF);
}

// Extract geometry type (bits 32-39) - for info only
uint GetGeometryType(uint64_t sortKey)
{
    return uint((sortKey >> 32) & 0xFF);
}

// Extract depth (bits 0-31)
uint GetDepth(uint64_t sortKey)
{
    return uint(sortKey & 0xFFFFFFFF);
}


#endif // SORT_KEY_HLSLI
