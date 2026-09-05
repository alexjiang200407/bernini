#pragma once
#include <array>
#include <bgl/LayerType.h>
#include <bgl/glm.h>
#include <bgl/types/ChannelRouteDesc.h>

namespace bgl
{
	struct LoosePbrMaterialDesc
	{
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		float     metallicFactor  = 1.0f;
		float     roughnessFactor = 1.0f;

		// Cutout; see PbrMaterialDesc. A loose material routes its alpha explicitly (baseColor[3]),
		// so unlike a baked one it can always sample a real alpha channel.
		LayerType layerType   = LayerType::kOpaque;
		float     alphaCutoff = 0.5f;

		// Back faces on a non-opaque layer; see PbrMaterialDesc.
		bool doubleSided = true;

		// Coverage against transmission; see PbrMaterialDesc.
		float transmissionFactor = 0.0f;

		// Dielectric F0 tint and specular strength; see PbrMaterialDesc.
		glm::vec3 specularColorFactor = glm::vec3(1.0f);
		float     specularFactor      = 1.0f;

		std::array<ChannelRouteDesc, 4> baseColor;  // R, G, B, A
		std::array<ChannelRouteDesc, 3> orm;        // AO, roughness, metallic
		std::array<ChannelRouteDesc, 2> normal;     // X, Y (Z reconstructed in shader)
	};
}
