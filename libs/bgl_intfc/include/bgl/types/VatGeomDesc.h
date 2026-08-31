#pragma once
#include <bgl/TextureAssetHandle.h>
#include <bgl/glm.h>

namespace bgl
{
	/** One vertex of a VAT geom's bind-pose mesh -- the full procedural layout, tightly packed. */
	struct VatVertex
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec4 tangent;
	};

	/** One clip's rows of the VAT texture pair; see VatGeomDesc. */
	struct VatClipDesc
	{
		uint32_t firstRow   = 0;  // texture V of frame 0
		uint32_t frameCount = 0;  // real frames; the bake pads a duplicate row after the last
		float    sampleRate = 30.0f;
		bool     loop       = false;
	};

	/**
	 * A rig's baked clip set, as textures already uploaded through AddTextureAsset: positions
	 * `R16G16B16A16_UNORM` unorm-packed in [boundsMin, boundsMax] -- one box over every frame of
	 * every clip -- and normals `R8G8B8A8_UNORM`, `rgb` the unit object-space normal as
	 * `xyz * 0.5 + 0.5` and `a` the tangent's twist about it, `radians / 2pi + 0.5` (see
	 * docs/vat.md). Columns are geometry-local vertex indices; frame `f` of clip `c` is row
	 * `clips[c].firstRow + f`, which is the row index the shared idl::Clip carries as `firstFrame`.
	 *
	 * bgl never reads a `.bvat` (it stays codec-free); whoever decodes one -- gamelib, or a test
	 * synthesizing textures from scratch -- fills this in.
	 */
	struct VatGeomDesc
	{
		TextureAssetHandle positions;
		TextureAssetHandle normals;

		glm::vec3 boundsMin = glm::vec3(0.0f);
		glm::vec3 boundsMax = glm::vec3(1.0f);

		std::vector<VatClipDesc> clips;

		// Where each submesh's vertex columns start along U, in submesh order -- the bake's
		// VatColumns::columnBase values. Empty means a single submesh at column 0, which is what
		// AddVatMeshGeom uploads; AddVatMeshGeom requires one entry per submesh.
		std::vector<uint32_t> columnBases;
	};
}
