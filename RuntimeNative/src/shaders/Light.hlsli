#ifndef LIGHTS_HLSLI
#define LIGHTS_HLSLI

struct Light {
    float3 color;
    float intensity;

    float3 direction;
    int type;

    float shadowNearPlane;
    float range;
    float innerAngleCos;
    bool isCastingShadow;

    float3 position;
    float outerAngleCos;

    float2 shadowAtlasOffset;
    float2 shadowAtlasScale;

    row_major float4x4 lightSpaceMtx;
};

#endif