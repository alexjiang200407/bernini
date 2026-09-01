#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

namespace
{
	constexpr uint32_t c_Size = 256;

	bgl::GraphicsRef
	MakeGraphics()
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		opts.enablePixDebug           = true;
		return bgl::CreateGraphics(opts);
	}

	bgl::RenderTargetRef
	MakeTarget(bgl::IGraphics& gfx)
	{
		auto desc     = bgl::RenderTargetDesc();
		desc.width    = static_cast<int>(c_Size);
		desc.height   = static_cast<int>(c_Size);
		desc.headless = true;
		return gfx.CreateRenderTarget(desc);
	}

	constexpr uint32_t
	Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
		       (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
	}

	constexpr uint32_t c_OpaqueWhite = Rgba(255, 255, 255, 255);
	constexpr uint32_t c_OpaqueRed   = Rgba(255, 0, 0, 255);

	// Two triangles, the texture spanning the quad once.
	bgl::OverlayGeometryHandle
	Quad(bgl::IOverlay& overlay, float x0, float y0, float x1, float y1, uint32_t color)
	{
		const std::array<bgl::OverlayVertex, 4> vertices = { {
			{ { x0, y0 }, { 0.0f, 0.0f }, color },
			{ { x1, y0 }, { 1.0f, 0.0f }, color },
			{ { x1, y1 }, { 1.0f, 1.0f }, color },
			{ { x0, y1 }, { 0.0f, 1.0f }, color },
		} };
		const std::array<uint32_t, 6>           indices  = { 0, 1, 2, 0, 2, 3 };

		return overlay.CreateGeometry(vertices, indices);
	}

	// A 2x2 sRGB image: red, green on the top row, blue, white below.
	assetlib::ImageData
	Quadrants()
	{
		auto img                 = assetlib::ImageData();
		img.width                = 2;
		img.height               = 2;
		img.vkFormat             = assetlib::VkFormat::R8G8B8A8_SRGB;
		img.pixels               = core::fixed_buffer<std::byte>(16);
		const uint8_t texels[16] = { 255, 0, 0,   255, 0,   255, 0,   255,
			                         0,   0, 255, 255, 255, 255, 255, 255 };
		std::memcpy(img.pixels.data(), texels, sizeof(texels));
		img.subresources.push_back({ 0, 8, 16 });
		return img;
	}

	// The frame is a cleared scene with nothing drawn, so every pixel the overlay does not touch
	// is black.
	void
	DrawOverlayFrame(
		bgl::IGraphics&                   gfx,
		const bgl::RenderTargetRef&       target,
		const bgl::OverlayRef&            overlay,
		std::span<const bgl::OverlayDraw> draws)
	{
		gfx.BeginFrame(target);
		gfx.DrawOverlay(bgl::OverlayJob{ overlay, draws });
		gfx.EndFrame();
	}

	bool
	Near(float value, float expected, float tolerance = 0.03f)
	{
		return std::abs(value - expected) <= tolerance;
	}

	bool
	IsColor(const bgl::test::Rgba& c, float r, float g, float b)
	{
		return Near(c.r, r) && Near(c.g, g) && Near(c.b, b);
	}
}

TEST_CASE("Overlay", "[overlay][render]")
{
	auto gfx = MakeGraphics();
	REQUIRE(gfx != nullptr);

	auto target = MakeTarget(*gfx);
	REQUIRE(target != nullptr);

	auto overlay = gfx->CreateOverlay();
	REQUIRE(overlay != nullptr);

	SECTION("A textured quad lands where its pixels say")
	{
		const auto texture = overlay->CreateTexture(Quadrants(), "quadrants");
		const auto quad    = Quad(*overlay, 32.0f, 32.0f, 96.0f, 96.0f, c_OpaqueWhite);

		auto draw     = bgl::OverlayDraw();
		draw.geometry = quad;
		draw.texture  = texture;

		DrawOverlayFrame(*gfx, target, overlay, { &draw, 1 });

		const std::string png = "assets/golden/overlay_textured.got.png";
		gfx->ScreenshotPng(target, png);

		// Sampled linear, so the quad is a gradient between texel centres; the corners sit inside
		// the clamp region where each texel is pure.
		CHECK(IsColor(bgl::test::MeanColor(png, 34, 34, 8, 8), 1.0f, 0.0f, 0.0f));
		CHECK(IsColor(bgl::test::MeanColor(png, 86, 34, 8, 8), 0.0f, 1.0f, 0.0f));
		CHECK(IsColor(bgl::test::MeanColor(png, 34, 86, 8, 8), 0.0f, 0.0f, 1.0f));
		CHECK(IsColor(bgl::test::MeanColor(png, 86, 86, 8, 8), 1.0f, 1.0f, 1.0f));

		// The capture is the presented frame, overlay included -- and nothing beside the quad.
		CHECK(IsColor(bgl::test::MeanColor(png, 128, 128, 32, 32), 0.0f, 0.0f, 0.0f));
	}

	SECTION("A scissor clips a quad")
	{
		const auto quad = Quad(*overlay, 0.0f, 0.0f, 256.0f, 256.0f, c_OpaqueRed);

		auto draw     = bgl::OverlayDraw();
		draw.geometry = quad;
		draw.scissor  = bgl::OverlayRect{ 64, 64, 64, 64 };

		DrawOverlayFrame(*gfx, target, overlay, { &draw, 1 });

		const std::string png = "assets/golden/overlay_scissor.got.png";
		gfx->ScreenshotPng(target, png);

		CHECK(IsColor(bgl::test::MeanColor(png, 80, 80, 32, 32), 1.0f, 0.0f, 0.0f));
		CHECK(IsColor(bgl::test::MeanColor(png, 16, 16, 32, 32), 0.0f, 0.0f, 0.0f));
		CHECK(IsColor(bgl::test::MeanColor(png, 200, 200, 32, 32), 0.0f, 0.0f, 0.0f));
	}

	SECTION("Translation and transform move a quad")
	{
		const auto quad = Quad(*overlay, 0.0f, 0.0f, 32.0f, 32.0f, c_OpaqueRed);

		// Translated by 32, then scaled by 2 and moved 64 right by the matrix: (0..32) + 32 ->
		// (32..64) -> x 128..192, y 64..128. The matrix carries a translation of its own so a
		// transposed upload would put it in the wrong row and miss.
		auto draw        = bgl::OverlayDraw();
		draw.geometry    = quad;
		draw.translation = glm::vec2(32.0f, 32.0f);
		draw.transform   = glm::translate(glm::mat4(1.0f), glm::vec3(64.0f, 0.0f, 0.0f)) *
		                   glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 2.0f, 1.0f));

		DrawOverlayFrame(*gfx, target, overlay, { &draw, 1 });

		const std::string png = "assets/golden/overlay_transform.got.png";
		gfx->ScreenshotPng(target, png);

		CHECK(IsColor(bgl::test::MeanColor(png, 144, 80, 32, 32), 1.0f, 0.0f, 0.0f));
		CHECK(IsColor(bgl::test::MeanColor(png, 80, 80, 32, 32), 0.0f, 0.0f, 0.0f));
		CHECK(IsColor(bgl::test::MeanColor(png, 200, 80, 16, 16), 0.0f, 0.0f, 0.0f));
		CHECK(IsColor(bgl::test::MeanColor(png, 144, 136, 16, 16), 0.0f, 0.0f, 0.0f));
	}

	SECTION("A half-covered pixel is weighted by its alpha, not by its decoded alpha")
	{
		// White at 50%, premultiplied in sRGB as a UI library hands it over: (128,128,128,128).
		// Over black that is 0.5 in linear, which the sRGB backbuffer encodes as ~0.735. Decoding
		// the premultiplied bytes as-is would land 0.5^2.2 = 0.22 linear, encoded ~0.5.
		const auto quad = Quad(*overlay, 0.0f, 0.0f, 256.0f, 256.0f, Rgba(128, 128, 128, 128));

		auto draw     = bgl::OverlayDraw();
		draw.geometry = quad;

		DrawOverlayFrame(*gfx, target, overlay, { &draw, 1 });

		const std::string png = "assets/golden/overlay_blend.got.png";
		gfx->ScreenshotPng(target, png);

		const auto centre = bgl::test::MeanColor(png, 112, 112, 32, 32);
		CHECK(Near(centre.r, 0.735f));
		CHECK(Near(centre.g, 0.735f));
		CHECK(Near(centre.b, 0.735f));
	}

	SECTION("A frame with no overlay draws none, whatever the frame before it drew")
	{
		const auto quad = Quad(*overlay, 0.0f, 0.0f, 256.0f, 256.0f, c_OpaqueRed);

		auto draw     = bgl::OverlayDraw();
		draw.geometry = quad;

		DrawOverlayFrame(*gfx, target, overlay, { &draw, 1 });

		// A call with no draws is the same as no call.
		gfx->BeginFrame(target);
		gfx->DrawOverlay(bgl::OverlayJob{ overlay, {} });
		gfx->EndFrame();

		const std::string png = "assets/golden/overlay_none.got.png";
		gfx->ScreenshotPng(target, png);

		CHECK(IsColor(bgl::test::MeanColor(png, 112, 112, 32, 32), 0.0f, 0.0f, 0.0f));
	}

	SECTION("Handles are checked at submission")
	{
		const auto quad  = Quad(*overlay, 0.0f, 0.0f, 32.0f, 32.0f, c_OpaqueRed);
		auto       draw  = bgl::OverlayDraw();
		draw.geometry    = quad;
		const auto draws = std::span<const bgl::OverlayDraw>(&draw, 1);

		CHECK_THROWS_AS(gfx->DrawOverlay(bgl::OverlayJob{ overlay, draws }), bgl::GraphicsError);

		overlay->ReleaseGeometry(quad);
		CHECK_THROWS_AS(overlay->ReleaseGeometry(quad), bgl::GraphicsError);

		gfx->BeginFrame(target);
		CHECK_THROWS_AS(gfx->DrawOverlay(bgl::OverlayJob{ overlay, draws }), bgl::GraphicsError);
		CHECK_THROWS_AS(gfx->DrawOverlay(bgl::OverlayJob{ nullptr, draws }), bgl::GraphicsError);

		// Another overlay's handles are not this one's, even though the slots exist in both.
		auto other        = gfx->CreateOverlay();
		auto otherTexture = other->CreateTexture(Quadrants(), "elsewhere");
		auto otherQuad    = Quad(*other, 0.0f, 0.0f, 32.0f, 32.0f, c_OpaqueRed);
		auto live         = bgl::OverlayDraw();
		live.geometry     = Quad(*overlay, 0.0f, 0.0f, 32.0f, 32.0f, c_OpaqueRed);
		live.texture      = otherTexture;
		CHECK_THROWS_AS(
			gfx->DrawOverlay(bgl::OverlayJob{ overlay, { &live, 1 } }),
			bgl::GraphicsError);
		CHECK_THROWS_AS(overlay->ReleaseGeometry(otherQuad), bgl::GraphicsError);
		CHECK_THROWS_AS(overlay->ReleaseTexture(otherTexture), bgl::GraphicsError);

		// A job that fails validation queues nothing from it, the good draw included.
		auto good                                   = bgl::OverlayDraw();
		good.geometry                               = live.geometry;
		auto bad                                    = bgl::OverlayDraw();
		bad.geometry                                = quad;
		const std::array<bgl::OverlayDraw, 2> mixed = { good, bad };
		CHECK_THROWS_AS(gfx->DrawOverlay(bgl::OverlayJob{ overlay, mixed }), bgl::GraphicsError);

		// A scissor with a negative extent is refused rather than inverted.
		auto clipped     = bgl::OverlayDraw();
		clipped.geometry = live.geometry;
		clipped.scissor  = bgl::OverlayRect{ 100, 100, -50, 10 };
		CHECK_THROWS_AS(
			gfx->DrawOverlay(bgl::OverlayJob{ overlay, { &clipped, 1 } }),
			bgl::GraphicsError);
		gfx->EndFrame();

		gfx->ScreenshotPng(target, "assets/golden/overlay_refused.got.png");
		CHECK(IsColor(
			bgl::test::MeanColor("assets/golden/overlay_refused.got.png", 8, 8, 16, 16),
			0.0f,
			0.0f,
			0.0f));
	}

	SECTION("Geometry is validated before anything is allocated")
	{
		const std::array<bgl::OverlayVertex, 3> tri = { {
			{ { 0.0f, 0.0f }, { 0.0f, 0.0f }, c_OpaqueRed },
			{ { 1.0f, 0.0f }, { 1.0f, 0.0f }, c_OpaqueRed },
			{ { 1.0f, 1.0f }, { 1.0f, 1.0f }, c_OpaqueRed },
		} };

		const std::array<uint32_t, 3> outOfRange   = { 0, 1, 3 };
		const std::array<uint32_t, 4> notTriangles = { 0, 1, 2, 0 };

		CHECK_THROWS_AS(overlay->CreateGeometry(tri, outOfRange), bgl::GraphicsError);
		CHECK_THROWS_AS(overlay->CreateGeometry(tri, notTriangles), bgl::GraphicsError);
		CHECK_THROWS_AS(overlay->CreateGeometry({}, notTriangles), bgl::GraphicsError);
	}
}
