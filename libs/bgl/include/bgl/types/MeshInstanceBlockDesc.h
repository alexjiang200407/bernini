#pragma once
#include <bgl/types/GeomHandle.h>
#include <bgl/types/InstanceDesc.h>
#include <cstdint>
#include <utility>

namespace bgl
{
	/** The most placements one block holds: what one writer dispatch reaches. */
	constexpr uint32_t c_MaxMeshInstanceBlockCapacity = 65535u * 64u;

	/**
	 * A run of placements of one geom, static or skinned, that the GPU places every frame and the
	 * CPU never touches after creation -- see ISceneView::CreateMeshInstanceBlock.
	 */
	struct MeshInstanceBlockDesc
	{
		GeomHandle geom;

		// How many placements the block holds. Every one is drawn, culled and hidden or not, each
		// frame, whether or not its writer places it: capacity, not the live count, is what the
		// block costs. At most c_MaxMeshInstanceBlockCapacity.
		uint32_t capacity = 0;

		// What every placement of a skinned geom's block plays, on PoseSource::kAuto: one record
		// the whole block shares, each placement ahead of the clock by the offset its writer gives
		// it (ISkinnedMeshInstanceBlock::SetPlaybackOffset). Fixed for the block's life: a placement ahead
		// of the clock is already past a ramp that starts now, so no rewrite could keep its pose at
		// the previous frame's time. Not read for a static geom.
		SkinnedPlaybackDesc playback;

		template <typename Self>
		Self&&
		SetGeom(this Self&& self, GeomHandle geom) noexcept
		{
			self.geom = geom;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetCapacity(this Self&& self, uint32_t capacity) noexcept
		{
			self.capacity = capacity;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPlayback(this Self&& self, const SkinnedPlaybackDesc& playback) noexcept
		{
			self.playback = playback;
			return std::forward<Self>(self);
		}
	};
}
