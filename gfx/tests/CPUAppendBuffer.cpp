#include "buffer/AppendBuffer.h"
#include "graphics/Graphics.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("AppendBuffer", "[cpu_append_buffer][dynamic_buffer]")
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

		// 1. Add three elements
		uint32_t vIdx1 = buf.EmplaceBack(100);  // Physical 1
		uint32_t vIdx2 = buf.EmplaceBack(200);  // Physical 2
		uint32_t vIdx3 = buf.EmplaceBack(300);  // Physical 3 (Last)

		// 2. Erase the middle element (vIdx2)
		// This should move 300 (vIdx3) from Physical 3 to Physical 2
		buf.Erase(vIdx2);

		// 3. Verify stability
		// vIdx1 should still be 100
		CHECK(buf.At(vIdx1) == 100);

		// vIdx3 should still be 300, even though its physical location moved
		CHECK(buf.At(vIdx3) == 300);

		// Total size should be 3 (Null + 100 + 300)
		CHECK(buf.Size() == 3);
	}

	SECTION("Virtual ID Reuse")
	{
		auto buf = gfx::AppendBuffer<int>{ device, desc };

		uint32_t vIdx1 = buf.EmplaceBack(10);
		buf.Erase(vIdx1);

		// Adding a new element should reuse the freed Virtual ID
		uint32_t vIdx2 = buf.EmplaceBack(20);
		CHECK(vIdx1 == vIdx2);
		CHECK(buf.At(vIdx2) == 20);
	}

	SECTION("Null Index Safety")
	{
		auto buf = gfx::AppendBuffer<int>{ device, desc };

		REQUIRE_NOTHROW(buf.At(0));

		REQUIRE_THROWS(buf.Erase(0));
	}
}
