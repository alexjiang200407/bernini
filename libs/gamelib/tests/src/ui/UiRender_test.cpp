#include <gamelib/ui/UiRenderer.h>
#include <gamelib/ui/UiRuntime.h>

#include "ui/UiTree.h"
#include "util/GoldenImage.h"
#include "util/TestOptions.h"

#include <RmlUi/Core.h>
#include <assetlib/image_io.h>

#include <catch2/catch_approx.hpp>

namespace
{
	namespace fs = std::filesystem;

	constexpr uint32_t c_Size = 256;

	constexpr std::string_view c_Font = "Authored/Fonts/Lato-Regular.ttf";

	fs::path
	AssetRoot()
	{
		return fs::path("assets") / "Data";
	}

	bgl::GraphicsRef
	MakeGraphics()
	{
		// The suite's shape: the debug layer on, GPU-based validation left to the bgl_extended
		// suite, which is where --gpu-validation is plumbed.
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;
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

	// The repo's fixture tree, which is where the documents and the font live.
	assetlib::AssetStore
	FixtureStore()
	{
		return assetlib::AssetStore(AssetRoot());
	}
}

TEST_CASE("A document draws through the overlay", "[ui][render]")
{
	auto gfx = MakeGraphics();
	REQUIRE(gfx != nullptr);

	auto target = MakeTarget(*gfx);
	REQUIRE(target != nullptr);

	// The preview target is a solid green frame: what matters here is that a document's
	// `target://` image shows another target's output, not what drew it.
	auto preview = MakeTarget(*gfx);
	REQUIRE(preview != nullptr);

	const assetlib::AssetStore store = FixtureStore();

	game::UiRenderer renderer(*gfx, store);
	renderer.RegisterTarget("preview", preview);

	game::UiRuntime runtime(store, renderer.Interface());
	runtime.LoadFontFace(c_Font);

	game::UiContextPtr context = runtime.CreateContext("render", c_Size, c_Size);

	// One green frame into the preview, through the overlay so this case needs no scene.
	{
		auto                                    overlay = gfx->CreateOverlay();
		const std::array<bgl::OverlayVertex, 4> quad    = { {
			{ { 0.0f, 0.0f }, { 0.0f, 0.0f }, 0xFF00FF00u },
			{ { 256.0f, 0.0f }, { 1.0f, 0.0f }, 0xFF00FF00u },
			{ { 256.0f, 256.0f }, { 1.0f, 1.0f }, 0xFF00FF00u },
			{ { 0.0f, 256.0f }, { 0.0f, 1.0f }, 0xFF00FF00u },
		} };
		const std::array<uint32_t, 6>           indices = { 0, 1, 2, 0, 2, 3 };

		auto fill     = bgl::OverlayDraw();
		fill.geometry = overlay->CreateGeometry(quad, indices);

		gfx->BeginFrame(preview);
		gfx->DrawOverlay(bgl::OverlayJob{ overlay, { &fill, 1 } });
		gfx->EndFrame();
	}

	Rml::ElementDocument* document = context->LoadDocument("Authored/UI/hud.rml");
	REQUIRE(document != nullptr);
	document->Show();
	context->Get().Update();

	INFO(game::test::DumpTree(context->Get()));

	const std::string png = "assets/golden/ui_hud.got.png";

	gfx->BeginFrame(target);
	renderer.Render(*gfx, *context);
	gfx->EndFrame();
	gfx->ScreenshotPng(target, png);

	SECTION("the box, the text and the target-backed image are all on screen")
	{
		// The coloured box: vertex colour alone, no texture.
		const bgl::test::Rgba box = bgl::test::MeanColor(png, 32, 32, 32, 32);
		CHECK(box.r == Catch::Approx(0.8f).margin(0.08));
		CHECK(box.g == Catch::Approx(0.2f).margin(0.08));

		// The target-backed image shows the preview's green frame (ADR-14).
		const bgl::test::Rgba shown = bgl::test::MeanColor(png, 160, 32, 64, 64);
		CHECK(shown.g > 0.5f);
		CHECK(shown.r < 0.3f);

		// The line of text: glyphs come from a texture the renderer generated, so the band is
		// neither empty nor solid.
		const bgl::test::Rgba text = bgl::test::MeanColor(png, 16, 96, 120, 32);
		CHECK(text.Luma() > 0.02f);
		CHECK(text.Luma() < 0.9f);
	}

	SECTION("the frame matches the golden, and is not a blank one")
	{
		// Looser than the tree's 1e-4 default: the glyphs are rasterised by FreeType, and the
		// same document has to match on Metal and on D3D12, whose text differs by a hair.
		CHECK(bgl::test::MatchesGolden("assets/golden/ui_hud.exp.png", png, 2e-3f));

		// The guard: an empty frame must not match, so a golden regenerated from two blanks
		// cannot pass. Nothing is drawn into this one.
		const std::string emptyPng = "assets/golden/ui_hud_empty.got.png";
		gfx->BeginFrame(target);
		gfx->EndFrame();
		gfx->ScreenshotPng(target, emptyPng);

		CHECK_FALSE(bgl::test::MatchesGolden("assets/golden/ui_hud.exp.png", emptyPng, 2e-3f));
	}
}

TEST_CASE("An image loads from the mount and an unknown source does not", "[ui][render]")
{
	// A sandbox rather than the fixture tree, and a colour nothing else in the frame produces:
	// a failed load draws the overlay's default white, so asserting on a *white* texture could
	// not tell a decode from a failure.
	const fs::path root = fs::temp_directory_path() / "bernini_ui_images";
	fs::remove_all(root);
	fs::create_directories(root / "Authored/UI");
	fs::create_directories(root / "Authored/Fonts");
	fs::copy_file(AssetRoot() / c_Font, root / c_Font);

	{
		auto image      = assetlib::ImageData();
		image.width     = 4;
		image.height    = 4;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_UNORM;
		image.pixels    = core::fixed_buffer<std::byte>(4 * 4 * 4);

		for (size_t t = 0; t < 16; ++t)
		{
			image.pixels[t * 4 + 0] = std::byte{ 0x00 };
			image.pixels[t * 4 + 1] = std::byte{ 0x00 };
			image.pixels[t * 4 + 2] = std::byte{ 0xFF };
			image.pixels[t * 4 + 3] = std::byte{ 0xFF };
		}
		image.subresources.push_back({ 0, 16, 64 });

		assetlib::writeKTX2(
			image,
			root / "Authored/UI/blue.ktx2",
			false,
			assetlib::Ktx2Compression::kNone);
	}

	auto gfx = MakeGraphics();
	REQUIRE(gfx != nullptr);

	auto target = MakeTarget(*gfx);
	REQUIRE(target != nullptr);

	const assetlib::AssetStore store(root);

	game::UiRenderer renderer(*gfx, store);
	game::UiRuntime  runtime(store, renderer.Interface());
	runtime.LoadFontFace(c_Font);

	game::UiContextPtr context = runtime.CreateContext("images", c_Size, c_Size);

	Rml::ElementDocument* document = context->Get().LoadDocumentFromMemory(
		R"(<rml><head><style>
			body { width: 100%; height: 100%; }
			img { position: absolute; width: 64px; height: 64px; }
			#loaded { left: 32px; top: 32px; }
			#missing { left: 128px; top: 32px; }
			#unknown { left: 32px; top: 128px; }
			#foreign { left: 128px; top: 128px; }
		   </style></head>
		   <body>
			<img id="loaded" src="blue.ktx2"/>
			<img id="missing" src="absent.ktx2"/>
			<img id="unknown" src="target://nothing-registered"/>
			<img id="foreign" src="http://example.com/x.png"/>
		   </body></rml>)",
		"Authored/UI/images.rml");

	REQUIRE(document != nullptr);
	document->Show();
	context->Get().Update();

	const std::string png = "assets/golden/ui_images.got.png";

	gfx->BeginFrame(target);
	renderer.Render(*gfx, *context);
	gfx->EndFrame();
	gfx->ScreenshotPng(target, png);

	// Decoded through the store, sampled through an _SRGB view: blue, and unmistakably not the
	// white a failed load would have drawn.
	const bgl::test::Rgba shown = bgl::test::MeanColor(png, 40, 40, 48, 48);
	CHECK(shown.b > 0.5f);
	CHECK(shown.r < 0.2f);
	CHECK(shown.g < 0.2f);

	// A load that fails draws a white box rather than nothing: RmlUi keeps the element and hands
	// over its null texture handle, which the overlay samples as opaque white -- the same rule
	// that makes an untextured div take its vertex colour. Loud rather than silent, which is the
	// right failure for an authoring mistake, and pinned here because a UI author will meet it.
	const bgl::test::Rgba missing = bgl::test::MeanColor(png, 136, 40, 48, 48);
	const bgl::test::Rgba unknown = bgl::test::MeanColor(png, 40, 136, 48, 48);
	CHECK(missing.Luma() > 0.9f);
	CHECK(unknown.Luma() > 0.9f);

	// A scheme this runtime does not resolve is refused by name rather than parsed as a target
	// key -- `target://` is the only one, and stripping its length off anything else is how a
	// URL becomes a nonsense target name.
	const bgl::test::Rgba foreign = bgl::test::MeanColor(png, 136, 136, 48, 48);
	CHECK(foreign.Luma() > 0.9f);

	// And neither failure took the frame down with it: the one that loaded is still blue.
	CHECK(bgl::test::MeanColor(png, 40, 40, 48, 48).b > 0.5f);

	fs::remove_all(root);
}

// The transform column is the one RmlUi hands over as a matrix rather than as geometry, so a
// wrong majorness lands here and nowhere else. The hud document sets no `transform`, which means
// RmlUi passes identity and a transposed identity is still identity -- the golden above cannot
// see this, and the first version of this renderer transposed.
TEST_CASE("An RCSS transform moves a box where the matrix says", "[ui][render]")
{
	auto gfx = MakeGraphics();
	REQUIRE(gfx != nullptr);

	auto target = MakeTarget(*gfx);
	REQUIRE(target != nullptr);

	const assetlib::AssetStore store = FixtureStore();

	game::UiRenderer renderer(*gfx, store);
	game::UiRuntime  runtime(store, renderer.Interface());
	runtime.LoadFontFace(c_Font);

	game::UiContextPtr context = runtime.CreateContext("transform", c_Size, c_Size);

	Rml::ElementDocument* document = context->Get().LoadDocumentFromMemory(
		R"(<rml><head><style>
			body { width: 100%; height: 100%; }
			div { position: absolute; left: 16px; top: 16px; width: 64px; height: 64px;
			      background-color: #ffffff; transform: translateX(128px); }
		   </style></head>
		   <body><div id="moved"/></body></rml>)",
		"Authored/UI/transform.rml");

	REQUIRE(document != nullptr);
	document->Show();
	context->Get().Update();

	const std::string png = "assets/golden/ui_transform.got.png";

	gfx->BeginFrame(target);
	renderer.Render(*gfx, *context);
	gfx->EndFrame();
	gfx->ScreenshotPng(target, png);

	// Laid out at x=16, translated by 128: the box covers 144..208. A transposed matrix puts the
	// translation in the bottom row, which is a projective smear rather than a move.
	CHECK(bgl::test::MeanColor(png, 152, 24, 48, 48).Luma() > 0.9f);

	// And it left where it was laid out.
	CHECK(bgl::test::MeanColor(png, 24, 24, 48, 48).Luma() < 0.02f);
}

// Alpha crosses three conversions between a stylesheet and the backbuffer: RmlUi premultiplies a
// colour in sRGB, the renderer packs it, and the shader divides the alpha out to decode and
// multiplies it back. bgl_extended pins that arithmetic on a hand-built vertex; this pins the half
// of it that a document actually drives.
TEST_CASE("A translucent element blends against what is under it", "[ui][render]")
{
	auto gfx = MakeGraphics();
	REQUIRE(gfx != nullptr);

	auto target = MakeTarget(*gfx);
	REQUIRE(target != nullptr);

	const assetlib::AssetStore store = FixtureStore();

	game::UiRenderer renderer(*gfx, store);
	game::UiRuntime  runtime(store, renderer.Interface());
	runtime.LoadFontFace(c_Font);

	game::UiContextPtr context = runtime.CreateContext("alpha", c_Size, c_Size);

	Rml::ElementDocument* document = context->Get().LoadDocumentFromMemory(
		R"(<rml><head><style>
			body { width: 100%; height: 100%; }
			div { position: absolute; width: 96px; height: 96px; }
			#solid { left: 16px; top: 16px; background-color: #cc3333; }
			#veil  { left: 64px; top: 64px; background-color: rgba(255,255,255,128); }
		   </style></head>
		   <body><div id="solid"/><div id="veil"/></body></rml>)",
		"Authored/UI/alpha.rml");

	REQUIRE(document != nullptr);
	document->Show();
	context->Get().Update();

	const std::string png = "assets/golden/ui_alpha.got.png";

	gfx->BeginFrame(target);
	renderer.Render(*gfx, *context);
	gfx->EndFrame();
	gfx->ScreenshotPng(target, png);

	// The two boxes overlap over 64..112; the veil alone covers 112..160.
	const bgl::test::Rgba solid = bgl::test::MeanColor(png, 24, 24, 24, 24);
	const bgl::test::Rgba over  = bgl::test::MeanColor(png, 76, 76, 24, 24);
	const bgl::test::Rgba veil  = bgl::test::MeanColor(png, 124, 124, 24, 24);

	// White at 50% over black is 0.5 in linear, which the sRGB backbuffer encodes as ~0.735 --
	// the same value bgl_extended's overlay case pins, reached here through RCSS instead. Decoding
	// the premultiplied bytes as-is would land ~0.5, and ignoring alpha would land 1.0.
	CHECK(veil.r == Catch::Approx(0.735f).margin(0.03));
	CHECK(veil.g == Catch::Approx(0.735f).margin(0.03));
	CHECK(veil.b == Catch::Approx(0.735f).margin(0.03));

	// The solid box is untouched where nothing covers it.
	CHECK(solid.r == Catch::Approx(0.8f).margin(0.05));
	CHECK(solid.g == Catch::Approx(0.2f).margin(0.05));

	// And where they overlap the veil lightened the red rather than replacing it: every channel
	// above the solid box, and the red still ahead of the green it was mixed into.
	CHECK(over.r > solid.r);
	CHECK(over.g > solid.g + 0.2f);
	CHECK(over.b > solid.b + 0.2f);
	CHECK(over.r > over.g);
}
