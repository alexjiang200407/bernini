#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

namespace
{
	constexpr uint32_t kWidth  = 600;
	constexpr uint32_t kHeight = 800;

	bgl::GraphicsOptions
	CaptureOptions()
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		return opts;
	}

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

	bgl::RenderTargetRef
	HeadlessTarget(const bgl::GraphicsRef& gfx)
	{
		auto desc     = bgl::RenderTargetDesc();
		desc.width    = static_cast<int>(kWidth);
		desc.height   = static_cast<int>(kHeight);
		desc.headless = true;
		return gfx->CreateRenderTarget(desc);
	}

	// Spends `ticket` by polling, the way an asynchronous caller would.
	assetlib::ImageData
	PollUntilResolved(const bgl::GraphicsRef& gfx, bgl::CaptureTicket ticket)
	{
		while (true)
		{
			if (auto image = gfx->TryResolveCapture(ticket); image.has_value())
				return std::move(*image);
		}
	}
}

// The split-phase pair must hand back exactly what the blocking path does: both read the same
// last-presented backbuffer, and the blocking path is itself submit + wait + resolve.
TEST_CASE("A split-phase capture resolves to the frame a blocking screenshot returns", "[capture]")
{
	auto gfx = bgl::CreateGraphics(CaptureOptions());
	REQUIRE(gfx != nullptr);

	auto target = HeadlessTarget(gfx);
	REQUIRE(target != nullptr);

	auto scene = gfx->CreateScene(CubeSceneDesc());
	auto view  = gfx->CreateSceneView(scene, 8);
	view->CreateStaticMeshInstance(scene->AddCubeGeom(), glm::mat4(1.0f));

	// Two frames so the presented backbuffer holds a fully uploaded scene.
	gfx->DrawFrame(target, CubeJob(view));
	gfx->DrawFrame(target, CubeJob(view));

	const bgl::CaptureTicket ticket = gfx->SubmitCapture(target);
	REQUIRE(ticket.IsValid());

	const assetlib::ImageData resolved = PollUntilResolved(gfx, ticket);

	// The ticket was spent by the resolve that returned the image.
	REQUIRE_THROWS_AS(gfx->TryResolveCapture(ticket), bgl::GraphicsError);

	const assetlib::ImageData blocking = gfx->ScreenshotToMemory(target);

	REQUIRE(resolved.width == blocking.width);
	REQUIRE(resolved.height == blocking.height);
	REQUIRE(resolved.pixels.size() == blocking.pixels.size());
	CHECK(std::memcmp(resolved.pixels.data(), blocking.pixels.data(), resolved.pixels.size()) == 0);
}

TEST_CASE("Capture tickets are bounded, single-spend, and discardable", "[capture]")
{
	auto gfx = bgl::CreateGraphics(CaptureOptions());
	REQUIRE(gfx != nullptr);

	auto target = HeadlessTarget(gfx);
	REQUIRE(target != nullptr);

	gfx->BeginFrame(target);
	REQUIRE_THROWS_AS(gfx->SubmitCapture(target), bgl::GraphicsError);
	gfx->EndFrame();

	REQUIRE_THROWS_AS(gfx->TryResolveCapture(bgl::CaptureTicket{}), bgl::GraphicsError);
	REQUIRE_NOTHROW(gfx->DiscardCapture(bgl::CaptureTicket{}));

	// Fill every slot; one more must refuse rather than silently block or drop a capture.
	std::vector<bgl::CaptureTicket> tickets;
	for (uint32_t i = 0; i < bgl::IGraphics::c_MaxPendingCaptures; ++i)
		tickets.push_back(gfx->SubmitCapture(target));
	REQUIRE_THROWS_AS(gfx->SubmitCapture(target), bgl::GraphicsError);

	// A discard frees its slot for the next submit, and discarding twice is a no-op.
	gfx->DiscardCapture(tickets.front());
	REQUIRE_NOTHROW(gfx->DiscardCapture(tickets.front()));
	REQUIRE_THROWS_AS(gfx->TryResolveCapture(tickets.front()), bgl::GraphicsError);

	const bgl::CaptureTicket refilled = gfx->SubmitCapture(target);
	REQUIRE(refilled.IsValid());

	// Drain what is still live so teardown finds no surprises.
	for (uint32_t i = 1; i < tickets.size(); ++i) PollUntilResolved(gfx, tickets[i]);
	PollUntilResolved(gfx, refilled);
}
