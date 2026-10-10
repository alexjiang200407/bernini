#include "Windows/LodSelector.h"
#include "Windows/MeshEditor/lod_view.h"

#include <QObject>
#include <QString>
#include <qstringliteral.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <editor_plugin_api/LanguageResolver.h>
#include <optional>
#include <vector>

namespace
{
	const editor::LanguageResolver c_Language;

	editor::MeshLods
	ThreeLevels()
	{
		auto lods      = editor::MeshLods();
		lods.minPixels = { 30.0f, 10.0f, 3.0f };
		return lods;
	}

	editor::LodReadout
	Drawing(uint32_t level)
	{
		return { .level = level, .pixels = 0.0f };
	}
}

// The selector is what three windows show, so what it says for a given preview state is pinned
// here once, with no preview behind it.
TEST_CASE("The Level of Detail selector lists Auto and then every level", "[lodselector][lod]")
{
	editor::LodSelector selector(c_Language);

	// Nothing shown: Auto alone, and nothing to pin.
	REQUIRE(selector.count() == 1);
	CHECK(selector.currentText() == QStringLiteral("Auto"));
	CHECK_FALSE(selector.isEnabled());
	CHECK_FALSE(selector.GetForcedLevel().has_value());

	const editor::MeshLods lods = ThreeLevels();
	selector.Refresh(&lods, std::nullopt);
	REQUIRE(selector.count() == 4);
	CHECK(selector.isEnabled());
	CHECK(selector.itemText(1) == QStringLiteral("LOD 0"));
	CHECK(selector.itemText(3) == QStringLiteral("LOD 2"));
	CHECK(selector.currentIndex() == 0);

	SECTION("Auto names the level the preview draws, or that it draws nothing")
	{
		selector.ShowAuto(Drawing(1));
		CHECK(selector.itemText(0) == QStringLiteral("Auto: LOD 1"));

		selector.ShowAuto(Drawing(3));
		CHECK(selector.itemText(0) == QStringLiteral("Auto: nothing drawn"));

		selector.ShowAuto(std::nullopt);
		CHECK(selector.itemText(0) == QStringLiteral("Auto"));
	}

	SECTION("A pick reports the level, and Auto says no more while it holds")
	{
		auto reported = std::vector<std::optional<uint32_t>>();
		QObject::connect(
			&selector,
			&editor::LodSelector::ForcedLevelChanged,
			[&](std::optional<uint32_t> level) { reported.push_back(level); });

		selector.setCurrentIndex(3);
		REQUIRE(reported.size() == 1);
		CHECK(reported[0] == 2u);
		CHECK(selector.GetForcedLevel() == 2u);

		selector.ShowAuto(Drawing(0));
		CHECK(selector.itemText(0) == QStringLiteral("Auto"));

		// A refresh keeps the pin where the list still reaches it, silently.
		selector.Refresh(&lods, 2);
		CHECK(selector.currentIndex() == 3);
		CHECK(reported.size() == 1);

		// And starts from Auto where it does not.
		auto fewer      = editor::MeshLods();
		fewer.minPixels = { 30.0f, 0.0f };
		selector.Refresh(&fewer, 2);
		CHECK(selector.currentIndex() == 0);
		CHECK_FALSE(selector.GetForcedLevel().has_value());
		CHECK(reported.size() == 1);

		// Back to Auto is a pick too, reported as no level.
		selector.setCurrentIndex(2);
		selector.setCurrentIndex(0);
		REQUIRE(reported.size() == 3);
		CHECK(reported[1] == 1u);
		CHECK_FALSE(reported[2].has_value());
	}

	SECTION("A mesh with an impostor lists it last, and pins it")
	{
		auto withImpostor     = ThreeLevels();
		withImpostor.impostor = true;
		selector.Refresh(&withImpostor, std::nullopt);
		REQUIRE(selector.count() == 5);
		CHECK(selector.itemText(4) == QStringLiteral("Impostor"));

		// Past the last level Auto names it, where a mesh without one draws nothing.
		selector.ShowAuto(Drawing(3));
		CHECK(selector.itemText(0) == QStringLiteral("Auto: Impostor"));

		selector.setCurrentIndex(4);
		CHECK(selector.GetForcedLevel() == editor::c_ForceImpostor);

		// A refresh keeps it pinned, and a mesh with none starts from Auto.
		selector.Refresh(&withImpostor, editor::c_ForceImpostor);
		CHECK(selector.currentIndex() == 4);
		selector.Refresh(&lods, editor::c_ForceImpostor);
		CHECK(selector.currentIndex() == 0);
	}

	SECTION("A mesh with no levels disables the selector again")
	{
		selector.Refresh(nullptr, std::nullopt);
		CHECK(selector.count() == 1);
		CHECK_FALSE(selector.isEnabled());
	}
}
