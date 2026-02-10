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

	struct CreatePBRMaterialOptions
	{
		const char* pName;

		const char* pAlbedoPath;
		GfxVec4     albedoColor;

		const char* pMetallicRoughnessPath;
		float       metallicFactor;
		float       roughnessFactor;

		const char* pNormalPath;
		float       normalScale;

		const char* pOcclusionPath;
		float       occlusionStrength;

		const char* pEmissivePath;
		GfxVec3     emissiveFactor;

		MaterialAlphaMode alphaMode;
		float             alphaCutoff;

		int32_t doubleSided;
	};

	GfxResult
	createPBRMaterial(Gfx gfx, CreatePBRMaterialOptions color);

#ifdef __cplusplus
}
#endif

#endif
