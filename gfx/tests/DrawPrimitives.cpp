#include "graphics/Graphics.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Draw Square", "[square][mesh]")
{
	auto gfxDesc                     = GfxOptions{};
	gfxDesc.headless                 = false;
	gfxDesc.width                    = 800;
	gfxDesc.height                   = 600;
	gfxDesc.enableGPUValidationLayer = true;
	gfxDesc.enableDebugLayer         = true;

	auto gfx = std::unique_ptr<gfx::IGraphics>{ gfx::IGraphics::Create(gfxDesc) };
	//gfx->GetMeshFactory().CreateCubeInstance(glm::mat4(1.0f));
}
