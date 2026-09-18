#pragma once
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl_common/idl/DrawBucket.h>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bgl
{
	/**
	 * What a draw bucket draws: the key its id was allocated for. Everything a pass needs to build or
	 * pick the bucket's kernels derives from these three -- the pixel program from (material,
	 * layer), the geometry program from geom, the raster state from material and layer.
	 */
	struct DrawBucketDesc
	{
		GeomType     geom;
		MaterialType material;
		LayerType    layer;
	};

	/**
	 * Hands out dense draw-bucket ids per distinct (geometry kind x material kind x layer), on
	 * first use -- a draw bucket being every instance one indirect dispatch per pass draws. The id is simultaneously the histogram slot, the prefix-sum lane, the indirect-arg
	 * element and every pass's kernel-table index, so it is allocated once here and nowhere
	 * derived.
	 *
	 * Bucket 0 is always (kStaticMesh, kNull, kOpaque): the fallback a demand past the ceiling
	 * clamps to -- visibly unlit, never a crash and never silently absent. A refused key is
	 * reported once. Skinned geometry has no unlit bucket, so a refused skinned key draws through
	 * the static fallback, unposed.
	 *
	 * Not synchronized: bgl is thread-affine (docs/bgl_api.md), and both the resolvers and Draw
	 * run on the one driving thread.
	 */
	class DrawBucketTable final
	{
	public:
		/** @pre ceiling >= 1 and <= idl::cMaxDrawBuckets. Tests shrink it to reach the clamp. */
		explicit DrawBucketTable(uint32_t ceiling = idl::cMaxDrawBuckets);

		DrawBucketTable(const DrawBucketTable&) = delete;
		DrawBucketTable(DrawBucketTable&&)      = delete;

		DrawBucketTable&
		operator=(const DrawBucketTable&) = delete;

		DrawBucketTable&
		operator=(DrawBucketTable&&) = delete;

		/**
		 * The bucket for the key, allocated if this is its first use. kNull and kAssert shade no
		 * base color, so they resolve to their opaque bucket whatever the layer.
		 *
		 * A skinned tier with a material that is neither kPBR nor a game surface is bgl's own bug
		 * here -- every door binding a material to animated geometry checks AcceptsMaterial first.
		 */
		[[nodiscard]] uint32_t
		Resolve(GeomType geom, MaterialType material, LayerType layer);

		/** The bucket a submesh draws through. An invalid handle resolves to the unlit fallback. */
		[[nodiscard]] uint32_t
		Resolve(GeomType geom, MaterialHandle material);

		/** @pre bucket < Count(). */
		[[nodiscard]] const DrawBucketDesc&
		Desc(uint32_t bucket) const noexcept;

		/** Allocated buckets. Ids below this are dense; the ceiling caps it. */
		[[nodiscard]] uint32_t
		Count() const noexcept
		{
			return static_cast<uint32_t>(m_Descs.size());
		}

		/** @pre bucket < Count(). Whether the bucket's instances draw from the depth-sorted list. */
		[[nodiscard]] bool
		Transparent(uint32_t bucket) const noexcept;

		/**
		 * One idl::DrawBucketFlag word per draw bucket, ceiling-sized: the upload source for the
		 * flags the GPU reads (TransparentDepthKeys).
		 */
		[[nodiscard]] std::span<const uint32_t>
		Flags() const noexcept
		{
			return m_Flags;
		}

	private:
		std::vector<DrawBucketDesc>            m_Descs;
		std::vector<uint32_t>                  m_Flags;
		std::unordered_map<uint64_t, uint32_t> m_KeyToDrawBucket;
		std::unordered_set<uint64_t>           m_Refused;
		uint32_t                               m_Ceiling;
	};
}
