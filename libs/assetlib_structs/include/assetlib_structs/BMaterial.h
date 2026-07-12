#pragma once
#include <core/glm.h>

namespace assetlib
{
	enum class MaterialMode : uint32_t
	{
		kBaked = 0,
		kLoose = 1,
	};

	enum class AlphaMode : uint32_t
	{
		kOpaque = 0,
		kMask   = 1,
	};

	struct ChannelRoute
	{
		std::string texture;      // path to the source texture file (empty when unrouted)
		uint16_t    channel = 0;  // 0 = R, 1 = G, 2 = B, 3 = A
	};

	struct SourceStamp
	{
		uint64_t size  = 0;
		int64_t  mtime = 0;  // seconds since the filesystem clock's epoch

		friend bool
		operator==(const SourceStamp&, const SourceStamp&) = default;
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

		AlphaMode alphaMode   = AlphaMode::kOpaque;
		float     alphaCutoff = 0.5f;

		std::array<ChannelRoute, c_LooseChannelCount> routes;

		std::array<SourceStamp, c_LooseChannelCount> routeStamps;

		std::string name;

		// The material editor's node graph, as an opaque JSON blob
		std::string editorGraph;
	};
}
