#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

namespace
{
	constexpr uint32_t kWidth  = 600;
	constexpr uint32_t kHeight = 800;
	constexpr int      kFrames = 12;

	bgl::SceneDesc
	CubeSceneDesc()
	{
		auto desc                    = bgl::SceneDesc();
		desc.maxGeom                 = 8;
		desc.maxMeshlets             = 512;
		desc.maxSubmeshes            = 8;
		desc.maxVertexBufferByteSize = 800000;
		desc.maxIndices              = 20000;
		return desc;
	}

	bgl::RenderJob
	CubeJob(const bgl::SceneViewRef& view)
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(kWidth) / static_cast<float>(kHeight),
				0.5f,
				500.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));
		return job;
	}

	// A small procedurally-filled RGBA8 image, so every frame can add a real texture asset (with
	// its deferred upload) without touching disk.
	assetlib::ImageData
	MakeImage(uint8_t seed)
	{
		constexpr uint32_t kDim = 4;

		auto img     = assetlib::ImageData();
		img.width    = kDim;
		img.height   = kDim;
		img.vkFormat = assetlib::VkFormat::R8G8B8A8_UNORM;
		img.pixels   = core::fixed_buffer<std::byte>(kDim * kDim * 4);

		for (size_t i = 0; i < img.pixels.size(); ++i)
			img.pixels.data()[i] = static_cast<std::byte>(seed + i);

		img.subresources.push_back({ 0, kDim * 4, kDim * kDim * 4 });
		return img;
	}

	// The frame-driving surface of one context, whichever API spells it -- IGraphics's implicit
	// primary or an explicit IRenderContext.
	struct ContextOps
	{
		std::function<bgl::RenderTargetRef(const bgl::RenderTargetDesc&)>       createTarget;
		std::function<void(const bgl::RenderTargetRef&, const bgl::RenderJob&)> drawFrame;
		std::function<void(const bgl::RenderTargetRef&, const std::string&)>    screenshot;
	};

	// One worker's life: own target, own scene, then kFrames of create-texture/draw/destroy churn
	// against the shared resource manager, and a clean shot at the end.
	void
	Drive(const bgl::GraphicsRef& gfx, const ContextOps& ops, const std::string& shot, uint8_t seed)
	{
		auto targetDesc     = bgl::RenderTargetDesc();
		targetDesc.width    = static_cast<int>(kWidth);
		targetDesc.height   = static_cast<int>(kHeight);
		targetDesc.headless = true;

		const bgl::RenderTargetRef target = ops.createTarget(targetDesc);
		const bgl::SceneRef        scene  = gfx->CreateScene(CubeSceneDesc());
		const bgl::SceneViewRef    view   = gfx->CreateSceneView(scene, 8);
		view->CreateStaticMeshInstance(scene->AddCubeGeom(), glm::mat4(1.0f));

		const bgl::RenderJob job = CubeJob(view);

		for (int frame = 0; frame < kFrames; ++frame)
		{
			const bgl::TextureAssetHandle transient =
				scene->AddTextureAsset(MakeImage(static_cast<uint8_t>(seed + frame)));

			ops.drawFrame(target, job);

			scene->DeleteTextureAsset(transient);
		}

		// One clean frame after the churn: the golden catches any slot the sweep freed too early.
		ops.drawFrame(target, job);
		ops.screenshot(target, shot);
	}
}

// The resource manager, its deletion gate, and the queue fence counters are shared by every
// context, so two threads driving two contexts hammer them concurrently: per frame each thread
// creates a texture asset (allocation + deferred upload), draws, then deletes it (retirement),
// while EndFrame's cleanup sweeps both timelines from both threads. The final frame of each must
// still be a correct cube -- the transient textures leave nothing behind.
TEST_CASE("Two threads drive two contexts concurrently", "[rendercontext][multicontext]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	auto gfx                      = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto ctxA = gfx->CreateRenderContext();
	auto ctxB = gfx->CreateRenderContext();
	REQUIRE(ctxA != nullptr);
	REQUIRE(ctxB != nullptr);

	const auto primaryOps = ContextOps{
		[&](const bgl::RenderTargetDesc& d) { return ctxA->CreateRenderTarget(d); },
		[&](const bgl::RenderTargetRef& t, const bgl::RenderJob& j) { ctxA->DrawFrame(t, j); },
		[&](const bgl::RenderTargetRef& t, const std::string& p) { ctxA->ScreenshotPng(t, p); },
	};
	const auto secondOps = ContextOps{
		[&](const bgl::RenderTargetDesc& d) { return ctxB->CreateRenderTarget(d); },
		[&](const bgl::RenderTargetRef& t, const bgl::RenderJob& j) { ctxB->DrawFrame(t, j); },
		[&](const bgl::RenderTargetRef& t, const std::string& p) { ctxB->ScreenshotPng(t, p); },
	};

	std::exception_ptr primaryError;
	std::exception_ptr secondError;

	std::thread primaryThread([&] {
		try
		{
			Drive(gfx, primaryOps, "assets/golden/concurrent_primary.got.png", 0);
		}
		catch (...)
		{
			primaryError = std::current_exception();
		}
	});

	std::thread secondThread([&] {
		try
		{
			Drive(gfx, secondOps, "assets/golden/concurrent_second.got.png", 128);
		}
		catch (...)
		{
			secondError = std::current_exception();
		}
	});

	primaryThread.join();
	secondThread.join();

	if (primaryError)
		std::rethrow_exception(primaryError);
	if (secondError)
		std::rethrow_exception(secondError);

	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/cube.exp.png",
			"assets/golden/concurrent_primary.got.png"));
	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/cube.exp.png",
			"assets/golden/concurrent_second.got.png"));
}
