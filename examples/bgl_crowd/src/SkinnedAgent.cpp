#include "SkinnedAgent.h"
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh.h>
#include <assetlib/import_document.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <core/err/util.h>
#include <format>
#include <gamelib/ClipInfo.h>
#include <headless/framing.h>
#include <headless/import_lookup.h>
#include <optional>
#include <utility>

namespace crowd_example
{
	SkinnedAgent
	LoadSkinnedAgent(
		const assetlib::AssetStore&  store,
		const std::filesystem::path& dataRoot,
		game::AssetManager&          assets,
		std::string_view             importKey,
		std::string_view             clipName)
	{
		const std::string documentKey = headless::ImportDocumentKey(importKey);
		if (!store.Exists(documentKey))
			core::throw_runtime_error("{} is not in {}", documentKey, dataRoot.string());

		const assetlib::ImportDocument document =
			assetlib::loadImportDocument(store.GetFiles(), documentKey);
		const std::string meshKey = document.GetMeshOutput();
		if (meshKey.empty())
			core::throw_runtime_error("{} produced no .bmesh", documentKey);
		headless::RequireDerived(store, meshKey, dataRoot);

		const std::string animationsKey = headless::AnimationOutput(document);
		if (animationsKey.empty())
			core::throw_runtime_error(
				"{} cooks no clip set, so it has nothing to play",
				documentKey);
		headless::RequireDerived(store, animationsKey, dataRoot);

		const auto model      = store.Load<assetlib::BMesh>(meshKey);
		const auto animations = store.Load<assetlib::AnimationSet>(animationsKey);
		headless::RequireDerived(store, animations.skeleton, dataRoot);
		const auto skeleton    = store.Load<assetlib::Skeleton>(animations.skeleton);
		const auto posedBounds = assetlib::findPosedBounds(animations, model, skeleton);

		auto                        agent = SkinnedAgent();
		std::vector<game::ClipInfo> clips;
		std::vector<uint32_t>       meshes;
		bool                        placed = false;
		for (uint32_t n = 0; n < model.nodes.size(); ++n)
		{
			const uint32_t meshIndex = model.nodes[n].mesh;
			if (meshIndex == assetlib::c_InvalidIndex || !assetlib::isSkinned(model, meshIndex))
				continue;

			// Every geom of a type is placed by one model matrix, so the character's meshes must
			// stand where its first does.
			const glm::mat4 world = headless::InstanceTransform(model, n);
			if (placed && world != agent.world)
			{
				core::throw_runtime_error(
					"{}'s skinned meshes stand at different places, and an agent type places all "
					"of its geoms alike",
					documentKey);
			}
			agent.world = world;
			placed      = true;

			const assetlib::Bounds posed =
				posedBounds[meshIndex] ?
					*posedBounds[meshIndex] :
					assetlib::posedBounds(model, meshIndex, skeleton, animations);
			game::AssetManager::SkinnedMesh acquired =
				assets.AcquireSkinnedMesh(document.source, document.source, {}, meshIndex, posed);
			agent.geoms.push_back(acquired.geom);
			clips = std::move(acquired.clips);
			meshes.push_back(meshIndex);
		}
		if (agent.geoms.empty())
			core::throw_runtime_error("{} cooks no skinned mesh", documentKey);

		agent.clipIndex    = headless::FindClip(clips, clipName);
		agent.cycleSeconds = clips[agent.clipIndex].duration;

		// Scaled to the playing clip's poses alone: a clip set whose clips travel would otherwise
		// size the agent by how far it walks.
		auto playing  = animations;
		playing.clips = { animations.clips.at(agent.clipIndex) };
		playing.posedBoxes.clear();
		agent.bounds = headless::EmptyBounds();
		for (const uint32_t meshIndex : meshes)
		{
			headless::GrowBounds(
				agent.bounds,
				agent.world,
				assetlib::posedBounds(model, meshIndex, skeleton, playing));
		}
		return agent;
	}
}
