#ifndef LIGHT_HLSLI
#define LIGHT_HLSLI

const unsigned int LightType_Directional = 0;
const unsigned int LightType_Point = 1;


struct Light
{
    float3 position;
    float3 direction;
    float3 color;
    float intensity;
    float range;
    unsigned int type;
};


#endif
