#pragma once
#include <assetlib_structs/BMaterial.h>
#include <core/glm.h>

namespace assetlib::imp
{
	/**
	 * A glTF metallic-roughness material in flattened import form. Texture fields index directly into
	 * imp::BMeshImport::textures (0xFFFFFFFF when absent) -- the flattened counterpart of the modular
	 * BMaterial, which references the same textures by file path instead. ormTexture is the glTF
	 * metallic-roughness texture, interpreted as occlusion(R)/roughness(G)/metallic(B) -- occlusion is
	 * only correct when the asset packs it into the same texture (the common shared-ORM convention).
	 */
	struct BMaterialImport
	{
		uint32_t  baseColorTexture = 0xFFFFFFFFu;
		uint32_t  normalTexture    = 0xFFFFFFFFu;
		uint32_t  ormTexture       = 0xFFFFFFFFu;
		glm::vec4 baseColorFactor  = glm::vec4(1.0f);
		float     metallicFactor   = 1.0f;
		float     roughnessFactor  = 1.0f;

		/** glTF's BLEND lands here as kOpaque: the engine's PBR has no blended mode to map it to. */
		AlphaMode alphaMode   = AlphaMode::kOpaque;
		float     alphaCutoff = 0.5f;

		/**
		 * Whether metallic-roughness is really this material's shading model. False when it declares one
		 * the engine cannot represent (KHR_materials_unlit, KHR_materials_pbrSpecularGlossiness), whose
		 * fields above are then glTF's defaults rather than the author's intent.
		 */
		bool isPbr = true;

		uint32_t nameOffset = 0;
	};
}
