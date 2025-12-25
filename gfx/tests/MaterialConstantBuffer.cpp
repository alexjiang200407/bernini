#include "buffer/MaterialConstantBuffer.h"
#include "graphics/Graphics.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Material constant buffer", "[dynamic_constant_buffer][materials]")
{
	auto gfxDesc     = GfxOptions{};
	gfxDesc.headless = true;
	gfxDesc.width    = 800;
	gfxDesc.height   = 600;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	REQUIRE(gfx);

	auto device = gfx->GetDevice();

	SECTION("Simple test")
	{
		auto cb = gfx::MaterialConstantBuffer{ gfx->GetDevice(), "shaders/PS_Cbuf_test.cso"sv };

		CHECK(cb["a"].IsValid());
		CHECK(cb["b"].IsValid());
		CHECK(cb["c"].IsValid());
		CHECK(cb["d"].IsValid());

		CHECK(cb["a"].Size() == 16);
		CHECK(cb["b"].Size() == 8);
		CHECK(cb["c"].Size() == 4);
		CHECK(cb["d"].Size() == 4);

		CHECK(cb.GetTotalSize() == 32);
	}
}
