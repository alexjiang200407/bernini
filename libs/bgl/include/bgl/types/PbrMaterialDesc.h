#pragma once
#include <bgl/LayerType.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/glm.h>

namespace bgl
{
	struct PbrMaterialDesc
	{
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		float     metallicFactor  = 1.0f;
		float     roughnessFactor = 1.0f;

		LayerType layerType   = LayerType::kOpaque;
		float     alphaCutoff = 0.5f;

		// Whether a kMask, kHashed or kBlend surface draws its back faces; glTF's `doubleSided`. A
		// kOpaque surface draws its front faces only whatever this says. On by default, since every
		// such surface drew both sides before the flag existed.
		bool doubleSided = true;

		// What baseColorFactor.a means on a kBlend surface, and read by no other layer: 0 coverage
		// (hair, foliage), 1 transmission (glass). glTF's KHR_materials_transmission.
		float transmissionFactor = 0.0f;

		// glTF's KHR_materials_specular. The colour tints a dielectric's F0 away from grey; the
		// factor weights the whole specular lobe, so 0 is a surface with no reflection at all.
		glm::vec3 specularColorFactor = glm::vec3(1.0f);
		float     specularFactor      = 1.0f;

		// Optional material maps, from AddTextureAsset.
		TextureAssetHandle baseColorTexture;
		TextureAssetHandle normalTexture;
		TextureAssetHandle ormTexture;
	};
}
