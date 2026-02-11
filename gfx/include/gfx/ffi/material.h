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

#ifdef __cplusplus
}
#endif

#endif
