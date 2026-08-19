#include <schema/ElementView.h>
#include <schema/LayoutBuilder.h>
#include <schema/Schema.h>
#include <schema/convert.h>

#include <catch2/matchers/catch_matchers_string.hpp>

using namespace schema;
using Catch::Matchers::ContainsSubstring;

namespace
{
	struct Stamp
	{
		uint64_t size;
		uint64_t mtime;
	};

	struct Route
	{
		uint16_t             channel;
		uint16_t             pad;
		glm::vec3            tint;
		Stamp                stamp;
		std::array<Stamp, 2> history;
	};

	static_assert(sizeof(Route) == 64);

	Schema
	RouteSchema()
	{
		Schema schema;
		LayoutBuilder<Stamp>(schema, "Stamp")
			.AddField("size", &Stamp::size)
			.AddField("mtime", &Stamp::mtime)
			.Finish();
		LayoutBuilder<Route>(schema, "Route")
			.AddField("channel", &Route::channel)
			.AddField("pad", &Route::pad)
			.AddField("tint", &Route::tint)
			.AddField("stamp", &Route::stamp)
			.AddField("history", &Route::history)
			.Finish();
		return schema;
	}
}

TEST_CASE(
	"a view reads a stored field by name, widened to what the reader asks for",
	"[schema][view]")
{
	const Schema               schema = RouteSchema();
	const std::array<Route, 2> routes = {
		Route{ 3, 0, { 1.0f, 2.0f, 3.0f }, { 10, 20 }, { { { 1, 2 }, { 3, 4 } } } },
		Route{ 4, 0, { 4.0f, 5.0f, 6.0f }, { 30, 40 }, { { { 5, 6 }, { 7, 8 } } } }
	};
	const ElementView view(schema.GetLayoutRef("Route"), std::as_bytes(std::span(routes)));

	REQUIRE(view.GetCount() == 2);
	CHECK(view.Has("channel"));
	CHECK_FALSE(view.Has("hash"));

	CHECK(view.Get<uint16_t>("channel") == 3);
	CHECK(view.Get<uint32_t>("channel", 1) == 4);  // widened on the way out
	CHECK(view.Get<glm::vec3>("tint", 1) == glm::vec3(4.0f, 5.0f, 6.0f));

	const ElementView stamp = view.GetStruct("stamp", 1);
	CHECK(stamp.GetCount() == 1);
	CHECK(stamp.Get<uint64_t>("mtime") == 40);

	const ElementView history = view.GetStruct("history");
	CHECK(history.GetCount() == 2);
	CHECK(history.Get<uint64_t>("size", 1) == 3);
}

TEST_CASE("a view refuses what the layout does not say", "[schema][view]")
{
	const Schema      schema = RouteSchema();
	const Route       route{};
	const ElementView view(schema.GetLayoutRef("Route"), std::as_bytes(std::span(&route, 1)));

	REQUIRE_THROWS_WITH(
		view.Get<uint16_t>("hash"),
		ContainsSubstring("Route: no field named hash"));
	REQUIRE_THROWS_WITH(
		view.Get<uint8_t>("channel"),
		ContainsSubstring("Route.channel: file stores u16, read as u8"));
	REQUIRE_THROWS_WITH(
		view.Get<uint64_t>("stamp"),
		ContainsSubstring("Route.stamp: holds struct Stamp, read as a value"));
	REQUIRE_THROWS_WITH(
		view.GetStruct("channel"),
		ContainsSubstring("Route.channel: holds u16, read as a struct"));
	REQUIRE_THROWS_WITH(
		view.Get<uint16_t>("channel", 1),
		ContainsSubstring("Route: element 1 of 1"));
	REQUIRE_THROWS_WITH(
		ElementView(schema.GetLayoutRef("Route"), std::as_bytes(std::span(&route, 1)).first(10)),
		ContainsSubstring("not a whole number of 64-byte elements"));
}

TEST_CASE("sameLayout is structural and recursive", "[schema][convert]")
{
	const Schema a = RouteSchema();
	const Schema b = RouteSchema();
	CHECK(sameLayout(a.GetLayoutRef("Route"), b.GetLayoutRef("Route")));

	struct StampV2
	{
		uint64_t size;
		uint64_t hash;
	};
	struct RouteV2
	{
		uint16_t               channel;
		uint16_t               pad;
		glm::vec3              tint;
		StampV2                stamp;
		std::array<StampV2, 2> history;
	};
	Schema c;
	LayoutBuilder<StampV2>(c, "Stamp")
		.AddField("size", &StampV2::size)
		.AddField("hash", &StampV2::hash)
		.Finish();
	LayoutBuilder<RouteV2>(c, "Route")
		.AddField("channel", &RouteV2::channel)
		.AddField("pad", &RouteV2::pad)
		.AddField("tint", &RouteV2::tint)
		.AddField("stamp", &RouteV2::stamp)
		.AddField("history", &RouteV2::history)
		.Finish();
	// Route's own fields are byte-identical; only the nested Stamp renamed a field.
	CHECK_FALSE(sameLayout(a.GetLayoutRef("Route"), c.GetLayoutRef("Route")));
	CHECK(sameLayout(a.GetLayoutRef("Stamp"), b.GetLayoutRef("Stamp")));
	CHECK_FALSE(sameLayout(a.GetLayoutRef("Stamp"), c.GetLayoutRef("Stamp")));
}
