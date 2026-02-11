#include "ffi/util.h"
#include "scene/Scene.h"
#include <gfx/ffi/material.h>

GfxResult
createPBRMaterial(GfxScene scene, GfxPBRMaterialOpts options, GfxMaterial* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		gfx::ffi::validatePtr(out, "out");

		auto pbrMaterial            = gfx::PBRMaterial{};
		pbrMaterial.albedoColor     = gfx::ffi::toGlmVec4(options.albedoColor);
		pbrMaterial.emissiveFactor  = gfx::ffi::toGlmVec3(options.emissiveFactor);
		pbrMaterial.alphaCutoff     = options.alphaCutoff;
		pbrMaterial.metallicFactor  = options.metallicFactor;
		pbrMaterial.roughnessFactor = options.roughnessFactor;

		// TODO: Load textures from paths and set texture IDs in pbrMaterial

		out->id = scene_.CreatePBRMaterial(
			std::move(pbrMaterial),
			gfx::ffi::alphaMode2LayerType(options.alphaMode));

		out->type = GfxMaterialType_PBR;

		return GFX_RESULT_OK;
	});
}

GfxResult
attachPBRMaterial(GfxScene scene, GfxMeshInstance meshInstance, GfxMaterial pbrMaterial)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		scene_.AttachPBRMaterial(meshInstance, pbrMaterial.id);
		return GFX_RESULT_OK;
	});
}
