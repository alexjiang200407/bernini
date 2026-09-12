#pragma once
#include <bgl/LayerType.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/glm.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bgl
{
	/// One value a material sets, under the name the surface declared it as.
	struct SurfaceValueBinding
	{
		std::string name;

		// Components past the declared type's are ignored, so a float3 parameter reads xyz.
		glm::vec4 value = glm::vec4(0.0f);
	};

	/// One channel of a data slot's composite: the named component of one texture, feeding the
	/// component whose position in SurfaceTextureBinding::routes this route occupies.
	struct SurfaceChannelRoute
	{
		TextureAssetHandle texture;

		// 0..3 for r..a of `texture`; anything else throws at create.
		uint32_t channel = 0;
	};

	/// One texture a material binds, under the name the surface declared it as.
	struct SurfaceTextureBinding
	{
		std::string        name;
		TextureAssetHandle texture;

		/**
		 * A data slot's alternative to `texture`: component c gathered from routes[c], drawn by
		 * the reader with no composite anywhere (ADR-11). Four wide because a sample is — a
		 * route left null samples white for its component. Only a data slot takes routes, and a
		 * binding carrying both a texture and routes is refused: a whole binding *is* the
		 * identity routing, and the engine writes it as one.
		 */
		std::array<SurfaceChannelRoute, 4> routes{};
	};

	/**
	 * A material drawn by one of the surfaces the client registered.
	 *
	 * Everything the surface declares is set by name and in any order, because the surface's own
	 * module decides what those names are and the engine only learned them at startup. What this
	 * does not name takes the default the surface declared for it; a name the surface never
	 * declared is a mistake and throws, rather than being written somewhere harmless.
	 */
	struct SurfaceMaterialDesc
	{
		// Which registered surface draws it: a name from IGraphics::GetSurfaceTypes().
		std::string surface;

		LayerType layerType   = LayerType::kOpaque;
		float     alphaCutoff = 0.5f;

		// Whether a kMask, kHashed or kBlend surface draws its back faces; a kOpaque one draws its
		// front faces whatever this says, as with a PBR material.
		bool doubleSided = true;

		std::vector<SurfaceValueBinding>   values;
		std::vector<SurfaceTextureBinding> textures;
	};
}
