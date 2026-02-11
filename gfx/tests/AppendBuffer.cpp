#include "buffer/AppendBuffer.h"
#include "graphics/Graphics.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("AppendBuffer", "[append_buffer][dynamic_buffer]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	auto desc = gfx::AppendBufferDesc{}.SetIsIndexBuffer().SetName("CPU Append Buffer Test");

	SECTION("Create AppendBuffer")
	{
		auto buf = gfx::AppendBuffer<int>{ device, desc };
		CHECK(buf.Size() == 1);  // index 0 reserved
	}

	SECTION("Append AppendBuffer")
	{
		auto buf  = gfx::AppendBuffer<int>{ device, desc };
		auto vIdx = buf.EmplaceBack(10);
		REQUIRE_NOTHROW(buf.At(vIdx));
		CHECK(buf.At(vIdx) == 10);
	}

	SECTION("Handle Stability after SwapAndPop")
	{
		auto buf = gfx::AppendBuffer<int>{ device, desc };

		uint32_t vIdx1 = buf.EmplaceBack(100);  // Physical 1
		uint32_t vIdx2 = buf.EmplaceBack(200);  // Physical 2
		uint32_t vIdx3 = buf.EmplaceBack(300);  // Physical 3 (Last)

		buf.Erase(vIdx2);

		CHECK(buf.At(vIdx1) == 100);

		CHECK(buf.At(vIdx3) == 300);

		CHECK(buf.Size() == 3);
	}

	SECTION("Virtual ID Reuse")
	{
		auto buf = gfx::AppendBuffer<int>{ device, desc };

		uint32_t vIdx1 = buf.EmplaceBack(10);
		buf.Erase(vIdx1);

		uint32_t vIdx2 = buf.EmplaceBack(20);
		CHECK(vIdx1 == vIdx2);
		CHECK(buf.At(vIdx2) == 20);
	}

	SECTION("Null Index Safety")
	{
		auto buf = gfx::AppendBuffer<int>{ device, desc };

		REQUIRE_NOTHROW(buf.At(0));

		CHECK_FALSE(buf.Erase(0));
	}
}

TEST_CASE("AppendBuffer advanced behavior", "[append_buffer][advanced]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 1;
	gfxDesc.height   = 1;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();
	auto desc   = gfx::AppendBufferDesc{}.SetName("AppendBuffer Advanced Test");

	SECTION("Insertion order is preserved")
	{
		gfx::AppendBuffer<int> buf{ device, desc };

		auto a = buf.EmplaceBack(1);
		auto b = buf.EmplaceBack(2);
		auto c = buf.EmplaceBack(3);

		CHECK(buf.At(a) == 1);
		CHECK(buf.At(b) == 2);
		CHECK(buf.At(c) == 3);
	}

	SECTION("Erase last element")
	{
		gfx::AppendBuffer<int> buf{ device, desc };

		auto a = buf.EmplaceBack(10);
		auto b = buf.EmplaceBack(20);

		buf.Erase(b);

		CHECK(buf.At(a) == 10);
		CHECK(buf.Size() == 2);
	}

	SECTION("Erase first element with multiple entries")
	{
		gfx::AppendBuffer<int> buf{ device, desc };

		auto a = buf.EmplaceBack(10);
		auto b = buf.EmplaceBack(20);
		auto c = buf.EmplaceBack(30);

		buf.Erase(a);

		CHECK(buf.At(b) == 20);
		CHECK(buf.At(c) == 30);
	}

	SECTION("Buffer grows safely")
	{
		gfx::AppendBuffer<int> buf{ device, desc };

		for (int i = 0; i < 256; ++i) buf.EmplaceBack(i);

		for (int i = 0; i < 256; ++i) CHECK(buf.At(i + 1) == i);
	}

	SECTION("Erase all elements then reuse")
	{
		gfx::AppendBuffer<int> buf{ device, desc };

		std::vector<uint32_t> ids;
		for (int i = 0; i < 10; ++i) ids.push_back(buf.EmplaceBack(i));

		for (auto id : ids) buf.Erase(id);

		CHECK(buf.Size() == 1);

		auto newId = buf.EmplaceBack(99);
		CHECK(newId != 0);
		CHECK(buf.At(newId) == 99);
	}

	SECTION("Invalid access throws")
	{
		gfx::AppendBuffer<int> buf{ device, desc };

		auto id = buf.EmplaceBack(5);
		buf.Erase(id);

		REQUIRE_THROWS(buf.At(id));
		REQUIRE_THROWS(buf.At(999));
	}

	SECTION("Erase invalid ID throws")
	{
		gfx::AppendBuffer<int> buf{ device, desc };

		CHECK_FALSE(buf.Erase(999));
	}

	SECTION("Reserved index is never returned")
	{
		gfx::AppendBuffer<int> buf{ device, desc };

		for (int i = 0; i < 10; ++i) CHECK(buf.EmplaceBack(i) != 0);
	}

	SECTION("Move constructor preserves data")
	{
		gfx::AppendBuffer<int> a{ device, desc };

		auto id1 = a.EmplaceBack(11);
		auto id2 = a.EmplaceBack(22);

		gfx::AppendBuffer<int> b = std::move(a);

		CHECK(b.At(id1) == 11);
		CHECK(b.At(id2) == 22);
	}

	SECTION("Stress test: random erase and append")
	{
		gfx::AppendBuffer<int> buf{ device, desc };

		std::vector<uint32_t> ids;

		for (int i = 0; i < 100; ++i) ids.push_back(buf.EmplaceBack(i));

		for (int i = 0; i < 50; ++i) buf.Erase(ids[i]);

		for (int i = 0; i < 50; ++i) buf.EmplaceBack(100 + i);

		CHECK(buf.Size() >= 51);
	}
}
