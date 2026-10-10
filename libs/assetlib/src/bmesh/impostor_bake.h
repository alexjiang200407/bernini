#pragma once
#include <assetlib_structs/Mesh.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace assetlib
{
	/** An sRGB RGBA8 image, rows top first, as a base colour texture decodes to. */
	struct ImpostorImage
	{
		uint32_t                 width  = 0;
		uint32_t                 height = 0;
		std::span<const uint8_t> rgba;
	};

	/** What one submesh's material contributes to its impostor: the glTF's own base colour. */
	struct ImpostorSurface
	{
		glm::vec4            baseColorFactor = glm::vec4(1.0f);
		const ImpostorImage* baseColor   = nullptr;  // sampled through TEXCOORD_0; null for none
		bool                 doubleSided = false;
		bool                 alphaTest   = false;
		float                alphaCutoff = 0.5f;
	};

	/** The geometry a bake reads: one mesh entry's level 0, out of a cooked mesh's pools. */
	struct ImpostorSource
	{
		std::span<const Submesh>         submeshes;  // level 0's, in order
		std::span<const ImpostorSurface> surfaces;   // parallel to submeshes
		std::span<const std::byte>       vertexData;
		std::span<const std::byte>       indexData;
	};

	/** A baked impostor: its record with offsets 0 and c_ImpostorAtlasBytes, and those bytes. */
	struct BakedImpostor
	{
		MeshImpostor         record;
		std::vector<uint8_t> texels;  // the albedo atlas, then the normal-depth atlas
	};

	/**
	 * The direction frame (x, y) of the atlas was seen from, unit length, toward the viewer, y up:
	 * the hemi-octahedral decode of its grid point (x, y) / (c_ImpostorFramesPerSide - 1), so the
	 * corner frames look along the horizon and the middle of the grid from straight above. The
	 * impostor stage decodes the same way.
	 */
	[[nodiscard]] glm::vec3
	impostorFrameDirection(uint32_t x, uint32_t y) noexcept;

	/**
	 * Renders `source` from every frame's direction into the two atlases MeshImpostor describes,
	 * orthographically over its bounding sphere, and builds their mips. A face a single-sided
	 * material draws from its front only is culled from its back, so an inverted-hull outline
	 * stays an outline.
	 *
	 * @throws std::runtime_error if the source has no triangle to bake, or a submesh carries no
	 *         float position, or an index points outside its submesh.
	 */
	[[nodiscard]] BakedImpostor
	bakeImpostor(const ImpostorSource& source);
}
