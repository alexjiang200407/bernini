#pragma once
#include <assetlib_structs/SourceStamp.h>
#include <core/glm.h>

namespace assetlib
{
	enum class ShadingModel : uint32_t
	{
		kPbr = 0,
		kCount,
	};

	enum class AlphaMode : uint32_t
	{
		kOpaque = 0,
		kMask   = 1,
		kBlend  = 2,

		// Not a glTF mode: nothing imports it, and it is chosen in the material editor. Alpha becomes
		// stochastic coverage rather than a cutoff, which only resolves under temporal AA.
		kHashed = 3,
	};

	struct ChannelRoute
	{
		std::string texture;      // path to the source texture file (empty when unrouted)
		uint16_t    channel = 0;  // 0 = R, 1 = G, 2 = B, 3 = A
	};

	enum class PbrChannel : size_t
	{
		kBaseColorR = 0,
		kBaseColorG,
		kBaseColorB,
		kBaseColorA,

		kAo,
		kRoughness,
		kMetallic,

		kNormalX,
		kNormalY,

		kCount,
	};

	inline constexpr size_t c_LooseChannelCount = static_cast<size_t>(PbrChannel::kCount);

	/** A contiguous run of `routes` that the bake composites into one map. */
	struct ChannelGroup
	{
		PbrChannel first;
		size_t     count;
	};

	inline constexpr ChannelGroup c_BaseColorChannels{ PbrChannel::kBaseColorR, 4 };
	inline constexpr ChannelGroup c_OrmChannels{ PbrChannel::kAo, 3 };
	inline constexpr ChannelGroup c_NormalChannels{ PbrChannel::kNormalX, 2 };

	[[nodiscard]] inline constexpr size_t
	channelIndex(PbrChannel channel) noexcept
	{
		return static_cast<size_t>(channel);
	}

	/** The index of the `component`-th channel of `group` in `PbrParams::routes`. */
	[[nodiscard]] inline constexpr size_t
	channelIndex(const ChannelGroup& group, size_t component) noexcept
	{
		return channelIndex(group.first) + component;
	}

	static_assert(
		c_BaseColorChannels.count + c_OrmChannels.count + c_NormalChannels.count ==
			c_LooseChannelCount,
		"The channel groups must partition routes exactly; a channel in none of them is never "
		"baked");

	struct PbrParams
	{
		std::string baseColorTexture;  // path to the base-color texture file (empty when absent)
		std::string normalTexture;     // path to the normal texture file (empty when absent)
		std::string ormTexture;        // path to the occlusion/roughness/metallic texture file
		glm::vec4   baseColorFactor = glm::vec4(1.0f);
		float       metallicFactor  = 1.0f;
		float       roughnessFactor = 1.0f;

		AlphaMode alphaMode   = AlphaMode::kOpaque;
		float     alphaCutoff = 0.5f;

		// What baseColorFactor.a means under AlphaMode::kBlend: 0 for coverage (hair, foliage), 1 for
		// transmission (glass, a lens), and read by no other mode. glTF's KHR_materials_transmission.
		float transmissionFactor = 0.0f;

		std::array<ChannelRoute, c_LooseChannelCount> routes;

		std::array<SourceStamp, c_LooseChannelCount> routeStamps;
	};

	struct BMaterial
	{
		std::string name;

		ShadingModel shadingModel = ShadingModel::kPbr;

		std::string editorGraph;

		PbrParams pbr;
	};
}
