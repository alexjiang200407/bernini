#pragma once

namespace gfx
{
	struct PBRMaterial
	{
		using ID = uint32_t;

		glm::vec4 albedoColor{ 1, 1, 1, 1 };

		glm::vec3 emissiveFactor = { 1.0f, 1.0f, 1.0f };
		float     alphaCutoff    = 0.0f;

		uint32_t albedoTexId            = 0;
		uint32_t metallicRoughnessTexId = 0;
		uint32_t normalTexId            = 0;
		uint32_t occlusionTexId         = 0;

		uint32_t emissiveTexId   = 0;
		float    roughnessFactor = 1.0f;
		float    metallicFactor  = 1.0f;
		float    normalScale     = 1.0f;
	};
}
