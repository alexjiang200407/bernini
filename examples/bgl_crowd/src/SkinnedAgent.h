#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib_structs/Bounds.h>
#include <bgl/glm.h>
#include <bgl/types/GeomHandle.h>
#include <cstdint>
#include <filesystem>
#include <gamelib/AssetManager.h>
#include <string>
#include <string_view>
#include <vector>

namespace crowd_example
{
	/** A character every agent of a type is drawn as: its skinned geoms, and the clip they loop. */
	struct SkinnedAgent
	{
		std::vector<bgl::GeomHandle> geoms;

		// Where the import puts the character, shared by every geom of it.
		glm::mat4 world = glm::mat4(1.0f);

		// The box the playing clip's poses fill, placed by `world`: what the agent is scaled to.
		assetlib::Bounds bounds;

		uint32_t clip = 0;

		// One cycle of the clip at rate 1: the spread that gives every agent a phase of its own.
		float cycleSeconds = 0.0f;
	};

	/**
	 * Acquires every skinned mesh `importKey` (a `.bimport` or `.glb` under `dataRoot`) cooks, with
	 * `clipName` its clip, or its first when empty.
	 *
	 * @throws std::runtime_error if the import is missing, cooks no clip set or no skinned mesh,
	 *         its derived containers are not on disk, its skinned meshes stand at different places,
	 *         or it has no clip of that name.
	 */
	[[nodiscard]] SkinnedAgent
	LoadSkinnedAgent(
		const assetlib::AssetStore&  store,
		const std::filesystem::path& dataRoot,
		game::AssetManager&          assets,
		std::string_view             importKey,
		std::string_view             clipName);
}
