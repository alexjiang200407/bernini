#pragma once
#include <assetlib_structs/BMaterial.h>
#include <core/glm.h>
#include <cstdint>
#include <string>
#include <vector>

namespace assetlib::imp
{
	/** One `bernini_<field>` number or array of a material's extras: a value the surface sets. */
	struct SurfaceValueImport
	{
		std::string field;
		glm::vec4   value = glm::vec4(0.0f);
		uint32_t    width = 0;  // 1 to 4 -- how many of `value`'s components the extras gave
	};

	/** One `bernini_<field>` string of a material's extras: the image a surface's slot binds. */
	struct SurfaceSlotImport
	{
		std::string field;
		uint32_t    texture = 0xFFFFFFFFu;  // into imp::BMeshImport::textures, decoded as data
	};

	/**
	 * What a glTF material's `extras` say about the surface it draws with, as written: a material's
	 * Custom Properties in Blender, exported flat. `bernini_surface` names the surface and every
	 * other `bernini_<field>` sets one of its fields -- a number or an array of 1 to 4 numbers for a
	 * value, a string naming one of the file's images for a slot. Keys without the prefix are not
	 * ours and are skipped.
	 *
	 * Nothing here is checked against the surface: assetlib does not hold the surfaces a project
	 * registers, so whether `field` exists, and is as wide as `width`, is its importer's question.
	 * An empty `surfaceName` means the material names no surface and imports as glTF describes it.
	 */
	struct SurfaceImport
	{
		std::string                     surfaceName;
		std::vector<SurfaceValueImport> values;
		std::vector<SurfaceSlotImport>  textures;  // `textures`, not `slots`: Qt defines that
	};

	/**
	 * A glTF material in flattened import form, always as metallic-roughness -- a
	 * specular-glossiness one is converted on the way in. Texture fields index directly into
	 * imp::BMeshImport::textures (0xFFFFFFFF when absent) -- the flattened counterpart of the modular
	 * BMaterial, which references the same textures by file path instead. ormTexture is the glTF
	 * metallic-roughness texture, which specifies only roughness(G) and metallic(B); its red channel
	 * carries occlusion only under the shared-ORM convention. occlusionTexture is glTF's own
	 * occlusion map and takes precedence over that red channel wherever it is present.
	 * geometryOcclusionTexture is the same map when glTF addresses it through TEXCOORD_1: geometry AO on a
	 * second UV set, kept beside the ORM rather than folded into it.
	 */
	struct BMaterialImport
	{
		uint32_t  baseColorTexture         = 0xFFFFFFFFu;
		uint32_t  normalTexture            = 0xFFFFFFFFu;
		uint32_t  ormTexture               = 0xFFFFFFFFu;
		uint32_t  occlusionTexture         = 0xFFFFFFFFu;
		uint32_t  geometryOcclusionTexture = 0xFFFFFFFFu;
		glm::vec4 baseColorFactor          = glm::vec4(1.0f);
		float     metallicFactor           = 1.0f;
		float     roughnessFactor          = 1.0f;

		AlphaMode alphaMode   = AlphaMode::kOpaque;
		float     alphaCutoff = 0.5f;

		// glTF's `doubleSided`, and its default: a face is drawn from its front only.
		bool doubleSided = false;

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

		// The surface the material's extras name, independent of isPbr: an importer that does not
		// know the surface still has the glTF's own fields to fall back on.
		SurfaceImport surface;

		uint32_t nameOffset = 0;
	};
}
