#include "scene/transparent_sort.h"
#include <bgl/PsoType.h>

namespace
{
	constexpr uint32_t kPbr = static_cast<uint32_t>(bgl::PsoType::kTransparent_StaticMesh_PBR);
	constexpr uint32_t kLoose =
		static_cast<uint32_t>(bgl::PsoType::kTransparent_StaticMesh_LoosePbr);

	// The dense instance indices, in the order BuildTransparentDrawOrder emitted them.
	std::vector<uint32_t>
	order(std::vector<bgl::TransparentDrawable> drawables, std::vector<bgl::TransparentRun>& runs)
	{
		std::vector<uint32_t> out;
		bgl::BuildTransparentDrawOrder(drawables, out, runs);
		return out;
	}
}

TEST_CASE("Transparent draw order is back-to-front", "[transparent]")
{
	std::vector<bgl::TransparentRun> runs;

	// depth is a sort key; larger is farther. Instances 0..2 supplied near-to-far.
	auto out = order({ { 1.0f, kPbr, 0 }, { 5.0f, kPbr, 1 }, { 3.0f, kPbr, 2 } }, runs);

	// Farthest (depth 5, index 1) first, nearest (depth 1, index 0) last.
	CHECK(out == std::vector<uint32_t>{ 1, 2, 0 });
}

TEST_CASE("Transparent order depends only on depth, not input order", "[transparent]")
{
	std::vector<bgl::TransparentRun> runsA;
	std::vector<bgl::TransparentRun> runsB;

	const auto a = order({ { 1.0f, kPbr, 0 }, { 2.0f, kPbr, 1 }, { 3.0f, kPbr, 2 } }, runsA);

	// Same drawables, supplied in the opposite order.
	const auto b = order({ { 3.0f, kPbr, 2 }, { 2.0f, kPbr, 1 }, { 1.0f, kPbr, 0 } }, runsB);

	CHECK(a == b);
}

TEST_CASE("Equal depths break ties deterministically by instance index", "[transparent]")
{
	std::vector<bgl::TransparentRun> runs;

	const auto a = order({ { 4.0f, kPbr, 7 }, { 4.0f, kPbr, 2 }, { 4.0f, kPbr, 5 } }, runs);
	const auto b = order({ { 4.0f, kPbr, 5 }, { 4.0f, kPbr, 7 }, { 4.0f, kPbr, 2 } }, runs);

	CHECK(a == std::vector<uint32_t>{ 2, 5, 7 });
	CHECK(a == b);
}

TEST_CASE("Runs are maximal spans of one PSO in depth order", "[transparent]")
{
	std::vector<bgl::TransparentRun> runs;

	// Depth order is far->near: loose(6), pbr(4), pbr(3), loose(1). The two adjacent PBRs are one
	// run; the PSO switches force three runs total, not four.
	const auto out = order(
		{ { 3.0f, kPbr, 0 }, { 6.0f, kLoose, 1 }, { 1.0f, kLoose, 2 }, { 4.0f, kPbr, 3 } },
		runs);

	REQUIRE(out == std::vector<uint32_t>{ 1, 3, 0, 2 });

	REQUIRE(runs.size() == 3);

	CHECK(runs[0].pso == kLoose);
	CHECK(runs[0].offset == 0);
	CHECK(runs[0].count == 1);

	CHECK(runs[1].pso == kPbr);
	CHECK(runs[1].offset == 1);
	CHECK(runs[1].count == 2);

	CHECK(runs[2].pso == kLoose);
	CHECK(runs[2].offset == 3);
	CHECK(runs[2].count == 1);

	// Offsets and counts tile the list with no gaps or overlaps.
	uint32_t expectedOffset = 0;
	for (const bgl::TransparentRun& run : runs)
	{
		CHECK(run.offset == expectedOffset);
		expectedOffset += run.count;
	}
	CHECK(expectedOffset == out.size());
}

TEST_CASE("An empty transparent set yields no order and no runs", "[transparent]")
{
	std::vector<bgl::TransparentRun> runs;
	const auto                       out = order({}, runs);

	CHECK(out.empty());
	CHECK(runs.empty());
}
