#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string_view>
#include <utility>

using namespace assetlib;

namespace
{
	/** Two clips as the cook inferred them: Walk_InPlace as a one-shot, Run_InPlace as a loop. */
	AnimationSet
	InferredClips()
	{
		AnimationSet set;
		for (const auto& [name, loop] :
		     std::array{ std::pair<std::string_view, uint32_t>{ "Walk_InPlace", 0u },
		                 std::pair<std::string_view, uint32_t>{ "Run_InPlace", 1u } })
		{
			AnimationClip clip{};
			clip.nameOffset = set.stringPool.add(name);
			clip.frameCount = 1;
			clip.loop       = loop;
			set.clips.push_back(clip);
		}
		return set;
	}
}

TEST_CASE("An authored loop overrules the one the cook inferred, both ways", "[skinning][cliploop]")
{
	AnimationSet set = InferredClips();

	const std::array authored = { ClipLoop{ "Walk_InPlace", true },
		                          ClipLoop{ "Run_InPlace", false } };
	applyClipLoops(set, authored);

	CHECK(set.clips[0].loop == 1u);
	CHECK(set.clips[1].loop == 0u);
}

TEST_CASE("An authored loop for a clip that is not there is ignored", "[skinning][cliploop]")
{
	AnimationSet set = InferredClips();

	const std::array authored = { ClipLoop{ "Sprint", true } };
	applyClipLoops(set, authored);

	CHECK(set.clips[0].loop == 0u);
	CHECK(set.clips[1].loop == 1u);
}
