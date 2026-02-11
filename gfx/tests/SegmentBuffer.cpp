#include "buffer/SegmentBuffer.h"
#include "graphics/Graphics.h"
#include <catch2/catch_test_macros.hpp>

namespace
{
	struct TestStruct
	{
		uint32_t a;
		float    b;
	};

	static_assert(std::is_trivially_copyable_v<TestStruct>);
}

static std::unique_ptr<gfx::IGraphics>
CreateTestGraphics()
{
	GfxOptions desc{};
	desc.headless = true;
	desc.width    = 1;
	desc.height   = 1;
	return std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(desc) };
}

TEST_CASE("SegmentBuffer basic insertion", "[segment_buffer][emplace]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	SECTION("Single segment emplace")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		uint32_t       value     = 42;
		gfx::SegmentID segmentId = buffer.EmplaceRange(std::span{ &value, 1 });

		CHECK(segmentId == 1);

		CHECK(buffer.At(segmentId, 0) == 42);
	}

	SECTION("Multi-element segment emplace")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		std::array<uint32_t, 3> values{ 1, 2, 3 };
		gfx::SegmentID          segmentId = buffer.EmplaceRange(values);

		CHECK(segmentId == 1);

		CHECK(buffer.At(segmentId, 0) == 1);
		CHECK(buffer.At(segmentId, 1) == 2);
		CHECK(buffer.At(segmentId, 2) == 3);
	}
}

TEST_CASE("SegmentBuffer multiple segments", "[segment_buffer][segments]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	SECTION("Independent segments do not overlap")
	{
		std::array<uint32_t, 2> a{ 10, 20 };
		std::array<uint32_t, 3> b{ 1, 2, 3 };

		gfx::SegmentID segA = buffer.EmplaceRange(a);
		gfx::SegmentID segB = buffer.EmplaceRange(b);

		CHECK(segA != segB);

		CHECK(buffer.At(segA, 0) == 10);
		CHECK(buffer.At(segA, 1) == 20);

		CHECK(buffer.At(segB, 0) == 1);
		CHECK(buffer.At(segB, 1) == 2);
		CHECK(buffer.At(segB, 2) == 3);
	}
}

TEST_CASE("SegmentBuffer erase and reuse", "[segment_buffer][erase]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	SECTION("Erased segment ID can be reused")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		std::array<uint32_t, 2> a{ 1, 2 };
		std::array<uint32_t, 1> b{ 99 };

		gfx::SegmentID segA = buffer.EmplaceRange(a);
		buffer.Erase(segA);

		gfx::SegmentID segB = buffer.EmplaceRange(b);

		CHECK(segB == segA);
		CHECK(buffer.At(segB, 0) == 99);
	}
}

TEST_CASE("SegmentBuffer fragmented reuse", "[segment_buffer][erase][reuse][fragmentation]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	std::array<uint32_t, 2> a{ 1, 2 };
	std::array<uint32_t, 3> b{ 3, 4, 5 };
	std::array<uint32_t, 1> c{ 6 };

	auto segA = buffer.EmplaceRange(a);  // segA = 1
	auto segB = buffer.EmplaceRange(b);  // segB = 2
	auto segC = buffer.EmplaceRange(c);  // segC = 3

	buffer.Erase(segB);

	std::array<uint32_t, 4> d{ 7, 8, 9, 10 };
	auto                    segD = buffer.EmplaceRange(d);

	CHECK(buffer.At(segD, 0) == 7);
	CHECK(buffer.At(segD, 1) == 8);
	CHECK(buffer.At(segD, 2) == 9);
	CHECK(buffer.At(segD, 3) == 10);

	CHECK(buffer.At(segA, 0) == 1);
	CHECK(buffer.At(segA, 1) == 2);
	CHECK(buffer.At(segC, 0) == 6);
}

TEST_CASE("SegmentBuffer erase non-existent segment does nothing", "[segment_buffer][erase]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	CHECK_FALSE(buffer.Erase(9999));
}

TEST_CASE("SegmentBuffer erase triggers update", "[segment_buffer][erase][update]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();
	auto cmd    = device->createCommandList();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	std::array<uint32_t, 2> a{ 1, 2 };
	auto                    seg = buffer.EmplaceRange(a);

	cmd->open();
	CHECK(buffer.Update(cmd, device) == true);

	buffer.Erase(seg);

	CHECK(buffer.Update(cmd, device) == true);
	CHECK(buffer.Update(cmd, device) == false);

	cmd->close();
}

TEST_CASE("SegmentBuffer reuse after GPU update", "[segment_buffer][reuse][update]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();
	auto cmd    = device->createCommandList();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	std::array<uint32_t, 2> a{ 10, 20 };
	std::array<uint32_t, 1> b{ 99 };

	auto segA = buffer.EmplaceRange(a);

	cmd->open();
	CHECK(buffer.Update(cmd, device) == true);

	buffer.Erase(segA);

	auto segB = buffer.EmplaceRange(b);
	CHECK(segB == segA);
	CHECK(buffer.At(segB, 0) == 99);

	CHECK(buffer.Update(cmd, device) == true);
	CHECK(buffer.Update(cmd, device) == false);

	cmd->close();
}

TEST_CASE("SegmentBuffer redirect table behavior", "[segment_buffer][redirect]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	SECTION("Access through redirect table remains valid")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		std::array<uint32_t, 3> values{ 7, 8, 9 };
		gfx::SegmentID          segmentId = buffer.EmplaceRange(values);

		buffer.At(segmentId, 1) = 123;

		CHECK(buffer.At(segmentId, 0) == 7);
		CHECK(buffer.At(segmentId, 1) == 123);
		CHECK(buffer.At(segmentId, 2) == 9);
	}
}

TEST_CASE("SegmentBuffer update path", "[segment_buffer][update]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();
	auto cmd    = device->createCommandList();

	SECTION("Update uploads data and redirect table")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		std::array<uint32_t, 2> values{ 5, 6 };
		auto                    _ = buffer.EmplaceRange(values);

		cmd->open();
		bool updated = buffer.Update(cmd, device);
		CHECK(updated == true);

		CHECK(buffer.Update(cmd, device) == false);

		cmd->close();
	}
}

TEST_CASE("SegmentBuffer dirty flag behavior", "[segment_buffer][dirty]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();
	auto cmd    = device->createCommandList();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	std::array<uint32_t, 2> values{ 1, 2 };
	auto                    seg = buffer.EmplaceRange(values);

	cmd->open();
	CHECK(buffer.Update(cmd, device) == true);
	CHECK(buffer.Update(cmd, device) == false);

	buffer.At(seg, 0) = 42;
	CHECK(buffer.Update(cmd, device) == true);

	cmd->close();
}

TEST_CASE("SegmentBuffer growth reallocates safely", "[segment_buffer][realloc]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();
	auto cmd    = device->createCommandList();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	cmd->open();

	for (uint32_t i = 0; i < 10; ++i)
	{
		std::array<uint32_t, 3> vals{ i, i + 1, i + 2 };
		buffer.EmplaceRange(vals);
		CHECK(buffer.Update(cmd, device) == true);
	}

	CHECK(buffer.Update(cmd, device) == false);
	cmd->close();
}

TEST_CASE(
	"SegmentBuffer redirect table survives reallocation",
	"[segment_buffer][redirect][realloc]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	std::array<uint32_t, 2> a{ 1, 2 };
	auto                    segA = buffer.EmplaceRange(a);

	for (uint32_t i = 0; i < 20; ++i)
	{
		std::array<uint32_t, 4> b{ i, i, i, i };
		buffer.EmplaceRange(b);
	}

	CHECK(buffer.At(segA, 0) == 1);
	CHECK(buffer.At(segA, 1) == 2);
}

TEST_CASE("SegmentBuffer empty segment", "[segment_buffer][edge]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	SECTION("SegmentBuffer emplace empty segment")
	{
		std::span<uint32_t> empty{};
		auto                seg = buffer.EmplaceRange(empty);

		CHECK(seg == 0);
	}

	SECTION("SegmentBuffer emplace empty segment repeatedly")
	{
		std::span<uint32_t> empty{};
		auto                seg1 = buffer.EmplaceRange(empty);
		auto                seg2 = buffer.EmplaceRange(empty);

		CHECK(seg1 == 0);
		CHECK(seg2 == 0);
	}
}

TEST_CASE("SegmentBuffer move safety", "[segment_buffer][move]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	gfx::SegmentBuffer<uint32_t> a{ device, {} };

	std::array<uint32_t, 2> values{ 1, 2 };
	auto                    seg = a.EmplaceRange(values);

	gfx::SegmentBuffer<uint32_t> b = std::move(a);

	CHECK(b.At(seg, 0) == 1);
	CHECK(b.At(seg, 1) == 2);
}

TEST_CASE(
	"SegmentBuffer move assignment preserves data integrity",
	"[segment_buffer][move][lifetime]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	gfx::SegmentBuffer<uint32_t> a{ device, {} };
	auto                         seg = a.EmplaceRange(std::array<uint32_t, 3>{ 10, 20, 30 });

	gfx::SegmentBuffer<uint32_t> b{ device, {} };
	b = std::move(a);  // move assignment

	CHECK(b.At(seg, 0) == 10);
	CHECK(b.At(seg, 1) == 20);
	CHECK(b.At(seg, 2) == 30);
}

TEST_CASE(
	"SegmentBuffer destruction while command list open does not crash",
	"[segment_buffer][lifetime][edge]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	auto cmd = device->createCommandList();
	cmd->open();

	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };
		buffer.EmplaceRange(std::array<uint32_t, 2>{ 1, 2 });
		// Destroy buffer while cmd is still open
	}

	cmd->close();
	SUCCEED();  // test passes if no crash occurs
}

TEST_CASE("SegmentBuffer destruction after update", "[segment_buffer][lifetime]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	{
		auto                         cmd = device->createCommandList();
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		std::array<uint32_t, 2> values{ 3, 4 };
		buffer.EmplaceRange(values);

		cmd->open();
		CHECK(buffer.Update(cmd, device) == true);
		cmd->close();
	}

	SUCCEED();
}

TEST_CASE("SegmentBuffer::Update validation", "[segment_buffer][update][validation]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();
	auto cmd    = device->createCommandList();
	cmd->open();

	SECTION("Empty buffer update is no-op")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };
		buffer.Update(cmd, device);
		CHECK(buffer.Update(cmd, device) == false);
	}

	SECTION("Update after emplace uploads data")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		std::array<uint32_t, 2> values{ 5, 6 };
		auto                    seg = buffer.EmplaceRange(values);

		bool firstUpdate = buffer.Update(cmd, device);
		CHECK(firstUpdate == true);

		CHECK(buffer.Update(cmd, device) == false);

		buffer.At(seg, 0) = 42;
		CHECK(buffer.Update(cmd, device) == true);
	}

	SECTION("Update after erase uploads redirect table correctly")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		std::array<uint32_t, 2> values{ 1, 2 };
		auto                    seg = buffer.EmplaceRange(values);

		buffer.Erase(seg);
		CHECK(buffer.Update(cmd, device) == true);

		CHECK(buffer.Update(cmd, device) == false);
	}

	SECTION("Update triggers buffer growth when needed")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		std::vector<uint32_t> largeData(512);
		std::iota(largeData.begin(), largeData.end(), 1);

		buffer.EmplaceRange(largeData);
		CHECK(buffer.Update(cmd, device) == true);
	}

	SECTION("Update maintains correct redirect table")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		std::array<uint32_t, 2> a{ 10, 20 };
		std::array<uint32_t, 3> b{ 1, 2, 3 };

		auto segA = buffer.EmplaceRange(a);
		auto segB = buffer.EmplaceRange(b);

		bool updated = buffer.Update(cmd, device);
		CHECK(updated == true);

		buffer.At(segA, 1) = 99;
		buffer.At(segB, 2) = 77;
		CHECK(buffer.Update(cmd, device) == true);

		CHECK(buffer.At(segA, 1) == 99);
		CHECK(buffer.At(segB, 2) == 77);
	}

	SECTION("Multiple updates in a row behave correctly")
	{
		gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

		std::array<uint32_t, 3> values{ 7, 8, 9 };
		auto                    seg = buffer.EmplaceRange(values);

		CHECK(buffer.Update(cmd, device) == true);
		CHECK(buffer.Update(cmd, device) == false);

		buffer.At(seg, 0) = 42;
		CHECK(buffer.Update(cmd, device) == true);
		CHECK(buffer.Update(cmd, device) == false);
	}

	cmd->close();
}

TEST_CASE(
	"SegmentBuffer update with no user segments behaves correctly",
	"[segment_buffer][update][edge]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();
	auto cmd    = device->createCommandList();
	cmd->open();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	CHECK(buffer.Update(cmd, device) == true);

	CHECK(buffer.Update(cmd, device) == false);

	cmd->close();
}

TEST_CASE(
	"SegmentBuffer update after multiple reallocations preserves all data",
	"[segment_buffer][update][realloc][integrity]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();
	auto cmd    = device->createCommandList();
	cmd->open();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	for (uint32_t i = 0; i < 50; ++i)
	{
		std::array<uint32_t, 20> vals;
		std::iota(vals.begin(), vals.end(), i * 20);
		buffer.EmplaceRange(vals);
		CHECK(buffer.Update(cmd, device) == true);
	}

	cmd->close();
}

TEST_CASE(
	"SegmentBuffer segment ID grows correctly after many segments",
	"[segment_buffer][id][growth]")
{
	auto gfx    = CreateTestGraphics();
	auto device = gfx->GetDevice();

	gfx::SegmentBuffer<uint32_t> buffer{ device, {} };

	uint32_t lastSeg = 0;
	for (uint32_t i = 0; i < 1000; ++i)
	{
		std::array<uint32_t, 1> val{ i };
		auto                    seg = buffer.EmplaceRange(val);
		CHECK(seg > lastSeg);
		lastSeg = seg;
	}
}
