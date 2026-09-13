#pragma once

#include "StoreAt.h"

#include <assetlib/codecs.h>  // IWYU pragma: keep
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <core/glm.h>
#include <filesystem>
#include <string>
#include <string_view>

// The two containers a rig's reference edges are read from, written with nothing in them but what
// the scan reads: enough for a query over the graph, and nothing a device or a bake would accept.
namespace editor::test
{
	/** A mesh that names its rig and nothing else -- a static attachment's shape. */
	inline void
	WriteMesh(
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& rel,
		std::string_view             skeleton)
	{
		auto mesh     = assetlib::BMesh();
		mesh.skeleton = std::string(skeleton);
		SaveAt(mesh, dataRoot / rel);
	}

	/** A minimal valid clip set, one two-frame clip, recording `skeleton` as its rig. */
	inline void
	WriteBanim(
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& rel,
		std::string_view             skeleton)
	{
		auto animations      = assetlib::AnimationSet();
		animations.skeleton  = std::string(skeleton);
		animations.boneCount = 1;

		auto clip        = assetlib::AnimationClip();
		clip.nameOffset  = animations.stringPool.add("walk");
		clip.firstSample = 0;
		clip.frameCount  = 2;
		clip.sampleRate  = 30.0f;
		clip.duration    = 1.0f / 30.0f;
		animations.clips.push_back(clip);

		for (int frame = 0; frame < 2; ++frame)
			animations.samples.push_back(
				{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });

		SaveAt(animations, dataRoot / rel);
	}
}
