#include <bgl/IGraphics.h>

namespace
{
	struct DdsSize
	{
		uint32_t width;
		uint32_t height;
	};

	DdsSize
	ReadDdsSize(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		REQUIRE(file.is_open());

		uint32_t magic = 0;
		file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
		REQUIRE(magic == 0x20534444u);  // "DDS "

		uint32_t header[4] = {};
		file.read(reinterpret_cast<char*>(header), sizeof(header));
		REQUIRE(file.good());

		return { header[3], header[2] };
	}

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = true;
		return opts;
	}

	bgl::RenderTargetHandle
	HeadlessTarget(const bgl::GraphicsHandle& gfx, int width, int height)
	{
		auto desc     = bgl::RenderTargetDesc();
		desc.width    = width;
		desc.height   = height;
		desc.headless = true;
		return gfx->CreateRenderTarget(desc);
	}

	bgl::SceneViewHandle
	MakeCubeScene(const bgl::GraphicsHandle& gfx)
	{
		auto desc                    = bgl::SceneDesc();
		desc.maxGeom                 = 8;
		desc.maxMeshlets             = 512;
		desc.maxSubmeshes            = 8;
		desc.maxVertexBufferByteSize = 800000;
		desc.maxIndices              = 20000;

		auto scene = gfx->CreateScene(desc);
		auto view  = gfx->CreateSceneView(scene, 8);
		auto cube  = scene->AddCubeGeom();
		view->CreateStaticMeshInstance(cube, glm::mat4(1.0f));
		return view;
	}

	void
	RenderFrame(
		const bgl::GraphicsHandle&     gfx,
		const bgl::RenderTargetHandle& target,
		const bgl::SceneViewHandle&    view,
		uint32_t                       width,
		uint32_t                       height)
	{
		auto       camera = bgl::Camera();
		const auto aspect = static_cast<float>(width) / static_cast<float>(height);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(width), static_cast<float>(height));

		gfx->DrawFrame(target, context);
	}
}

TEST_CASE("Resize recreates the backbuffers", "[resize][graphics]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto target = HeadlessTarget(gfx, 16, 8);
	REQUIRE(target != nullptr);

	const std::string before = "resize_before.dds";
	const std::string after  = "resize_after.dds";

	gfx->BeginFrame(target);
	gfx->EndFrame();
	gfx->ScreenshotRaw(target, before);

	const auto beforeSize = ReadDdsSize(before);
	CHECK(beforeSize.width == 16);
	CHECK(beforeSize.height == 8);

	gfx->Resize(target, 64, 32);

	gfx->BeginFrame(target);
	gfx->EndFrame();
	gfx->ScreenshotRaw(target, after);

	const auto afterSize = ReadDdsSize(after);
	CHECK(afterSize.width == 64);
	CHECK(afterSize.height == 32);

	CHECK_THROWS_AS(gfx->Resize(target, 0, 100), bgl::GraphicsError);
	CHECK_THROWS_AS(gfx->Resize(target, 100, 0), bgl::GraphicsError);

	std::filesystem::remove(before);
	std::filesystem::remove(after);
}

TEST_CASE("Resize with an existing static mesh instance", "[resize][graphics]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto target = HeadlessTarget(gfx, 32, 32);
	REQUIRE(target != nullptr);

	// A scene with a mesh instance already exists before the resize happens.
	auto scene = MakeCubeScene(gfx);

	gfx->Resize(target, 128, 96);

	// Drawing the pre-existing instance into the recreated backbuffers must work
	// and produce a frame at the new size.
	const std::string shot = "resize_mesh.dds";
	RenderFrame(gfx, target, scene, 128, 96);
	gfx->ScreenshotRaw(target, shot);

	const auto size = ReadDdsSize(shot);
	CHECK(size.width == 128);
	CHECK(size.height == 96);

	std::filesystem::remove(shot);
}

TEST_CASE("Resize after drawing a frame", "[resize][graphics]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto target = HeadlessTarget(gfx, 64, 64);
	REQUIRE(target != nullptr);

	auto scene = MakeCubeScene(gfx);

	const std::string before = "resize_draw_before.dds";
	const std::string after  = "resize_draw_after.dds";

	// Draw once at the original size.
	RenderFrame(gfx, target, scene, 64, 64);
	gfx->ScreenshotRaw(target, before);

	const auto beforeSize = ReadDdsSize(before);
	CHECK(beforeSize.width == 64);
	CHECK(beforeSize.height == 64);

	// Resize, then draw the same scene again into the new backbuffers. This
	// exercises the FrameGraph re-importing the recreated backbuffer/depth.
	gfx->Resize(target, 160, 120);
	RenderFrame(gfx, target, scene, 160, 120);
	gfx->ScreenshotRaw(target, after);

	const auto afterSize = ReadDdsSize(after);
	CHECK(afterSize.width == 160);
	CHECK(afterSize.height == 120);

	std::filesystem::remove(before);
	std::filesystem::remove(after);
}

TEST_CASE("Resize edge cases", "[resize][graphics]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto target = HeadlessTarget(gfx, 48, 48);
	REQUIRE(target != nullptr);

	const std::string shot = "resize_edge.dds";

	SECTION("Resizing to the same dimensions is a no-op")
	{
		gfx->Resize(target, 48, 48);

		gfx->BeginFrame(target);
		gfx->EndFrame();
		gfx->ScreenshotRaw(target, shot);

		const auto size = ReadDdsSize(shot);
		CHECK(size.width == 48);
		CHECK(size.height == 48);
	}

	SECTION("Consecutive resizes without rendering between them")
	{
		gfx->Resize(target, 100, 50);
		gfx->Resize(target, 13, 200);
		gfx->Resize(target, 77, 41);

		gfx->BeginFrame(target);
		gfx->EndFrame();
		gfx->ScreenshotRaw(target, shot);

		const auto size = ReadDdsSize(shot);
		CHECK(size.width == 77);
		CHECK(size.height == 41);
	}

	SECTION("Resize is rejected while a frame is active")
	{
		gfx->BeginFrame(target);
		CHECK_THROWS_AS(gfx->Resize(target, 64, 64), bgl::GraphicsError);
		gfx->EndFrame();

		// The rejected resize must leave the graphics usable; a later resize works.
		gfx->Resize(target, 80, 70);

		gfx->BeginFrame(target);
		gfx->EndFrame();
		gfx->ScreenshotRaw(target, shot);

		const auto size = ReadDdsSize(shot);
		CHECK(size.width == 80);
		CHECK(size.height == 70);
	}

	SECTION("Resize down to a 1x1 backbuffer")
	{
		gfx->Resize(target, 1, 1);

		gfx->BeginFrame(target);
		gfx->EndFrame();
		gfx->ScreenshotRaw(target, shot);

		const auto size = ReadDdsSize(shot);
		CHECK(size.width == 1);
		CHECK(size.height == 1);
	}

	SECTION("Resize larger then smaller across draws")
	{
		auto scene = MakeCubeScene(gfx);

		gfx->Resize(target, 256, 256);
		RenderFrame(gfx, target, scene, 256, 256);
		gfx->ScreenshotRaw(target, shot);
		CHECK(ReadDdsSize(shot).width == 256);

		gfx->Resize(target, 4, 4);
		RenderFrame(gfx, target, scene, 4, 4);
		gfx->ScreenshotRaw(target, shot);

		const auto size = ReadDdsSize(shot);
		CHECK(size.width == 4);
		CHECK(size.height == 4);
	}

	std::filesystem::remove(shot);
}
