#ifndef GFX_MATERIAL_H
#define GFX_MATERIAL_H
#include <gfx/ffi/common.h>

#ifdef __cplusplus
extern "C"
{
#endif

	enum MaterialAlphaMode : uint8_t
	{
		AlphaMode_Opaque = 0,
		AlphaMode_Mask   = 1,
		AlphaMode_Blend  = 2
	};

	struct GfxPBRMaterialOpts
	{
		GfxTexture albedoTex;
		GfxVec4    albedoColor;

		GfxTexture metallicRoughnessTex;
		float      metallicFactor;
		float      roughnessFactor;

		GfxTexture normalTex;
		float      normalScale;

		GfxTexture occlussionTex;
		float      occlusionStrength;

		GfxTexture emissiveTex;
		GfxVec3    emissiveFactor;

		MaterialAlphaMode alphaMode;
		float             alphaCutoff;

		int32_t doubleSided;
	};

	enum GfxMaterialType
	{
		GfxMaterialType_PBR = 0,
	};

	typedef struct
	{
		uint32_t        id;
		GfxMaterialType type;
	} GfxMaterial;

	GFX_API GfxResult
	createPBRMaterial(GfxScene scene, GfxPBRMaterialOpts options, GfxMaterial* out);

	GFX_API GfxResult
	attachPBRMaterial(GfxScene scene, GfxMeshInstance meshInstance, GfxMaterial pbrMaterial);

	GFX_API GfxResult
	createTexture(GfxScene scene, GfxTexture* out);

#ifdef __cplusplus
}
#endif

#endif
