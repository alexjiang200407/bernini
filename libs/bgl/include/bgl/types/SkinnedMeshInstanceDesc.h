#pragma once
#include <bgl/types/GeomHandle.h>
#include <bgl/types/InstanceDesc.h>
#include <core/glm.h>
#include <utility>

namespace bgl
{
	/**
	 * A skinned placement spawned on a whole playback record, posed from `source` -- see
	 * ISceneView::CreateSkinnedMeshInstance. One clip is SkinnedPlaybackDesc::FromClip.
	 *
	 * kBoneAnimTable is refused: a record of weighted slots has nowhere to live on a pose the rig
	 * shares. kAuto blends the record while the placement draws per instance and plays its heaviest
	 * slot from the table otherwise.
	 */
	struct SkinnedMeshInstanceDesc
	{
		GeomHandle          geom;
		glm::mat4           transform = glm::mat4(1.0f);
		SkinnedPlaybackDesc playback;
		PoseSource          source = PoseSource::kPerInstance;

		template <typename Self>
		Self&&
		SetGeom(this Self&& self, GeomHandle geom) noexcept
		{
			self.geom = geom;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTransform(this Self&& self, const glm::mat4& transform) noexcept
		{
			self.transform = transform;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPlayback(this Self&& self, const SkinnedPlaybackDesc& playback) noexcept
		{
			self.playback = playback;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSource(this Self&& self, PoseSource source) noexcept
		{
			self.source = source;
			return std::forward<Self>(self);
		}
	};
}
