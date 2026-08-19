#pragma once
#include <assetlib_structs/SourceStamp.h>
#include <core/glm.h>
#include <core/str/string_pool.h>

namespace assetlib
{
	/** D3D12's per-dimension texture cap. A bake past it in either direction is refused. */
	inline constexpr uint32_t c_MaxVatTextureDim = 16384;

	/**
	 * One clip's rows of the VAT texture pair. Frame `f` is row `firstRow + f`; row
	 * `firstRow + frameCount` duplicates the last frame, so fractional-V interpolation never
	 * bleeds into the clip stacked below.
	 */
	struct VatClip
	{
		uint32_t nameOffset;  // into BVat::stringPool

		uint32_t firstRow;    // texture V of frame 0
		uint32_t frameCount;  // real frames, padding row excluded

		/**
		 * Into BVat::palettes, frame-major like AnimationSet::samples: bone `b` of frame `f` is
		 * `palettes[firstPalette + f * boneCount + b]`.
		 */
		uint32_t firstPalette;

		float    sampleRate;  // Hz
		float    duration;    // seconds
		uint32_t loop;        // 1 when the last pose matches the first
	};

	static_assert(sizeof(VatClip) == 28);

	/**
	 * Where one submesh's vertices land along U: vertex `v` is column `columnBase + v`, the same
	 * submesh-local index the mesh shader's vertex map yields.
	 */
	struct VatColumns
	{
		uint32_t columnBase;
		uint32_t vertexCount;
	};

	static_assert(sizeof(VatColumns) == 8);

	/**
	 * A rig's clips baked to textures: every skinned vertex at every frame of every clip, as a
	 * position texture and a normal texture the crowd tier fetches by (vertex, frame) instead of
	 * skinning.
	 *
	 * A `.bvat` is wholly derived from the three inputs it stamps -- never committed, re-baked when
	 * any of them moves (see vatIsStale). The textures are embedded as encoded KTX2 payloads rather
	 * than referenced files because a VAT texture is a pure derivative of one rig's clip set, never
	 * shared -- with nothing shared there is nothing to reference, and deleting the asset is
	 * deleting the file.
	 */
	struct BVat
	{
		/**
		 * One AABB over every frame of every clip -- positions are unorm-packed in it, and per-clip
		 * boxes would make samples meaningless to interpolate or blend across clips. A texel unpacks
		 * as `boundsMin + texel * (boundsMax - boundsMin)`.
		 */
		glm::vec3 boundsMin;
		glm::vec3 boundsMax;

		uint32_t width     = 0;  // vertex columns, across every submesh
		uint32_t height    = 0;  // frame rows, per-clip padding included
		uint32_t boneCount = 0;

		std::vector<VatClip>    clips;
		std::vector<VatColumns> columns;  // per submesh, in the mesh's submesh order

		/**
		 * The skeletal side-channel: each real frame's skinning palette (model x inverseBind, what
		 * skinSubmesh consumes), addressed per clip -- see VatClip::firstPalette. A bone's
		 * model-space transform, for attachments, is its palette entry times the inverse of its
		 * `Bone::inverseBind`.
		 */
		std::vector<glm::mat4> palettes;

		// The bake's inputs: paths relative to the data root, and each file as the bake read it.
		std::string mesh;
		std::string skeleton;
		std::string animations;
		uint64_t    skeletonSignature = 0;
		SourceStamp meshStamp;
		SourceStamp skeletonStamp;
		SourceStamp animationsStamp;

		/**
		 * The texture pair, as encoded KTX2 byte streams (decodeKTX2 reads them): positions
		 * `R16G16B16A16_UNORM` packed in the bounds; normals `R8G8B8A8_UNORM`, `rgb` the unit
		 * object-space normal as `xyz * 0.5 + 0.5` and `a` the tangent's twist -- the turn about
		 * that normal, as `radians / 2pi + 0.5`, that the bind tangent carried onto it by the
		 * shortest arc still needs (see docs/vat.md). Empty on a tables-only read (loadVatTables),
		 * never in a full one.
		 */
		std::vector<std::byte> positionsKtx2;
		std::vector<std::byte> normalsKtx2;

		core::string_pool stringPool;
	};
}
