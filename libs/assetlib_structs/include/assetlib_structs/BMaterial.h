#pragma once
#include <core/glm.h>

namespace assetlib
{
	enum class MaterialMode : uint32_t
	{
		kBaked = 0,  // the baseColor / normal / orm triplet is authoritative
		kLoose = 1,  // the per-channel `routes` table is authoritative (triplet is the bake output)
	};

	struct ChannelRoute
	{
		std::string texture;      // path to the source texture file (empty when unrouted)
		uint16_t    channel = 0;  // 0 = R, 1 = G, 2 = B, 3 = A
	};

	inline constexpr size_t c_LooseChannelCount = 9;

	struct BMaterial
	{
		MaterialMode mode = MaterialMode::kBaked;

		std::string baseColorTexture;  // path to the base-color texture file (empty when absent)
		std::string normalTexture;     // path to the normal texture file (empty when absent)
		std::string ormTexture;        // path to the occlusion/roughness/metallic texture file
		glm::vec4   baseColorFactor = glm::vec4(1.0f);
		float       metallicFactor  = 1.0f;
		float       roughnessFactor = 1.0f;

		std::array<ChannelRoute, c_LooseChannelCount> routes;

		std::string name;

		// The material editor's node graph, as an opaque JSON blob (empty when the material was not
		// authored by the node editor). Nothing outside the editor interprets it: `routes` and the
		// triplet are the authoritative description, and this exists only so reopening a material
		// restores the graph that produced them -- node positions, unwired nodes and all.
		//
		// It is authoring data, so the exporter clears it when it bakes a material down to `kBaked`.
		std::string editorGraph;
	};
}
