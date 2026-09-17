#pragma once
#include <string_view>

namespace game::test
{
	// Any non-empty graph satisfies AssetCodec<BMaterial>::Serialize's node-graph requirement --
	// assetlib never reads its contents.
	inline constexpr std::string_view c_TestEditorGraph = R"({"connections":[],"nodes":[]})";
}
