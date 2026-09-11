#pragma once
#include <array>
#include <assetlib_structs/SourceStamp.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace assetlib
{
	enum class ShadingModel : uint32_t
	{
		// Factors, a baked triplet, and the channel routes behind it.
		kPbr = 0,

		// The same lighting, over a material half the game computes -- so there is no route, no
		// bake and no graph behind one. A game-defined *lighting* model would be a third value.
		kPbrSurface = 1,

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

	static_assert(
		c_BaseColorChannels.count + c_OrmChannels.count + c_NormalChannels.count ==
			c_LooseChannelCount,
		"The channel groups must partition routes exactly; a channel in none of them is never "
		"baked");

	/**
	 * The layer, which every shading model has and none owns: how alpha is read, and whether back
	 * faces draw. gamelib derives the renderer's LayerType from alphaMode.
	 */
	struct MaterialLayer
	{
		AlphaMode alphaMode   = AlphaMode::kOpaque;
		float     alphaCutoff = 0.5f;

		// Back faces on a cut-out, hashed or blended surface; opaque never draws them. True by
		// default, since every such material drew both sides before the key existed.
		bool doubleSided = true;
	};

	struct PbrParams
	{
		std::string baseColorTexture;  // path to the base-color texture file (empty when absent)
		std::string normalTexture;     // path to the normal texture file (empty when absent)
		std::string ormTexture;        // path to the occlusion/roughness/metallic texture file
		glm::vec4   baseColorFactor = glm::vec4(1.0f);
		float       metallicFactor  = 1.0f;
		float       roughnessFactor = 1.0f;

		// What baseColorFactor.a means under AlphaMode::kBlend: 0 for coverage (hair, foliage), 1 for
		// transmission (glass, a lens), and read by no other mode. glTF's KHR_materials_transmission.
		float transmissionFactor = 0.0f;

		// glTF's KHR_materials_specular: the colour tints a dielectric's F0, the factor weights the
		// whole specular lobe. 1 and white are glTF's defaults and the flat 0.04 dielectric.
		glm::vec3 specularColorFactor = glm::vec3(1.0f);
		float     specularFactor      = 1.0f;

		std::array<ChannelRoute, c_LooseChannelCount> routes;

		std::array<SourceStamp, c_LooseChannelCount> routeStamps;

		// assetlib::c_TextureBakeToken as it stood when the triplet was baked; zero before a bake, and
		// in a material from before it existed.
		uint64_t bakeToken = 0;
	};

	/**
	 * One value a surface material sets, under the name the surface declared it as.
	 *
	 * The binding, not the declaration: `bgl::SurfaceValue` is the field the shader declares, with
	 * its type, its offset and its default, and only the renderer has ever read the shader. The
	 * twin of this is `bgl::SurfaceValueBinding`, which this cannot be -- assetlib is the offline
	 * cook and links no renderer contract, exactly as `assetlib::VertexLayout` is not
	 * `idl::VertexLayout`.
	 */
	struct SurfaceValueBinding
	{
		std::string name;

		// One to four numbers, as the document wrote them. The count is the author's and not the
		// surface's: nothing here knows the declared type, and the renderer reads as many
		// components as the parameter has.
		std::vector<float> value;
	};

	/** One texture a surface material binds, under the name the surface declared it as. */
	struct SurfaceTextureBinding
	{
		std::string name;
		std::string texture;  // path to the texture file (empty when unbound)
	};

	/**
	 * What a material drawn by a game's own surface says: which surface, and what it sets on it.
	 *
	 * Nothing here is checked while the document is read. The names belong to a shader module the
	 * cook never sees, so a value naming no parameter is refused where the surface is known -- at
	 * the renderer, when the material is created -- and not at load.
	 */
	struct SurfaceParams
	{
		std::string                        name;
		std::vector<SurfaceValueBinding>   values;
		std::vector<SurfaceTextureBinding> textures;
	};

	struct BMaterial
	{
		std::string name;

		ShadingModel shadingModel = ShadingModel::kPbr;

		MaterialLayer layer;

		std::string editorGraph;

		PbrParams pbr;

		// Read when shadingModel is kPbrSurface, and left empty otherwise.
		SurfaceParams surface;

		// Document keys this build does not know, written back on save -- a sibling branch's new
		// field survives a round-trip through a reader that has never heard of it.
		std::string extraJson = "{}";
	};
}
