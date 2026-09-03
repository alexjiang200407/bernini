#pragma once
#include <assetlib_structs/BMaterial.h>
#include <core/glm.h>

namespace assetlib::imp
{
	/**
	 * A glTF material in flattened import form, always as metallic-roughness -- a
	 * specular-glossiness one is converted on the way in. Texture fields index directly into
	 * imp::BMeshImport::textures (0xFFFFFFFF when absent) -- the flattened counterpart of the modular
	 * BMaterial, which references the same textures by file path instead. ormTexture is the glTF
	 * metallic-roughness texture, which specifies only roughness(G) and metallic(B); its red channel
	 * carries occlusion only under the shared-ORM convention. occlusionTexture is glTF's own
	 * occlusion map and takes precedence over that red channel wherever it is present.
	 */
	struct BMaterialImport
	{
		uint32_t  baseColorTexture = 0xFFFFFFFFu;
		uint32_t  normalTexture    = 0xFFFFFFFFu;
		uint32_t  ormTexture       = 0xFFFFFFFFu;
		uint32_t  occlusionTexture = 0xFFFFFFFFu;
		glm::vec4 baseColorFactor  = glm::vec4(1.0f);
		float     metallicFactor   = 1.0f;
		float     roughnessFactor  = 1.0f;

		AlphaMode alphaMode   = AlphaMode::kOpaque;
		float     alphaCutoff = 0.5f;

		// KHR_materials_transmission's transmissionFactor; see PbrParams. Absent extension means 0,
		// which is glTF's own default and the coverage reading BLEND has always had here.
		float transmissionFactor = 0.0f;

		// KHR_materials_specular; see PbrParams. Absent extension means glTF's own defaults, which
		// are the flat 0.04 dielectric the renderer had before the extension was read.
		glm::vec3 specularColorFactor = glm::vec3(1.0f);
		float     specularFactor      = 1.0f;

		/**
		 * Whether the fields above are the author's intent. False only for KHR_materials_unlit, whose
		 * shading model the engine does not have, leaving them at glTF's defaults. Specular-glossiness
		 * is converted rather than refused, so it arrives true.
		 */
		bool isPbr = true;

		uint32_t nameOffset = 0;
	};
}
