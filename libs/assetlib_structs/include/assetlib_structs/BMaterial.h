#pragma once
#include <assetlib_structs/ShadingModel.h>
#include <assetlib_structs/SourceStamp.h>
#include <core/glm.h>

namespace assetlib
{
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

		// glTF's KHR_materials_specular: the colour tints a dielectric's F0, the factor weights the
		// whole specular lobe. 1 and white are glTF's defaults and the flat 0.04 dielectric.
		glm::vec3 specularColorFactor = glm::vec3(1.0f);
		float     specularFactor      = 1.0f;

		std::array<ChannelRoute, c_LooseChannelCount> routes;

		std::array<SourceStamp, c_LooseChannelCount> routeStamps;
	};

	/**
	 * Whether anything routes into `group`.
	 *
	 * A group with nothing routed bakes to no map at all, which is a complete bake rather than a
	 * missing one: the runtime substitutes white, flat normal or the factors alone.
	 */
	[[nodiscard]] inline bool
	groupIsRouted(const PbrParams& pbr, const ChannelGroup& group) noexcept
	{
		for (size_t i = 0; i < group.count; ++i)
			if (!pbr.routes[channelIndex(group, i)].texture.empty())
				return true;
		return false;
	}

	struct BMaterial
	{
		std::string name;

		ShadingModel shadingModel = ShadingModel::kPbr;

		std::string editorGraph;

		PbrParams pbr;

		// Document keys this build does not know, written back on save -- a sibling branch's new
		// field survives a round-trip through a reader that has never heard of it.
		std::string extraJson = "{}";
	};
}
