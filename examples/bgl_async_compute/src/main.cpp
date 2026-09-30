#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/GpuContext.h>
#include <core/err/util.h>
#include <core/hash.h>
#include <crowdlib/HashFillJob.h>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <vector>

// One GPU context, two owners: the renderer draws a spinning cube on its queue while crowdlib runs
// a kernel on the async one -- a D3D12 compute queue, or a second Metal command queue. Neither waits
// on the other. Each frame polls the kernel's fence; a finished result is read back, its checksum
// compared against the CPU's, and the next submission made. The log is the evidence: the fence
// values, the frames drawn while the kernel was in flight, and every checksum.

namespace
{
	struct Options
	{
		uint32_t width  = 800;
		uint32_t height = 600;

		// 0 draws until the window closes.
		uint32_t frames   = 0;
		uint32_t count    = 1u << 20;
		bool     headless = false;
	};

	uint64_t
	Checksum(std::span<const uint32_t> values) noexcept
	{
		return core::hash_bytes(values.data(), values.size_bytes(), core::hash_seed());
	}

	uint64_t
	ReferenceChecksum(uint32_t count, uint32_t seed)
	{
		auto values = std::vector<uint32_t>(count);
		for (uint32_t i = 0; i < count; ++i) values[i] = crowd::HashFillReference(i, seed);
		return Checksum(values);
	}

	int
	Run(const Options& opts)
	{
		std::optional<demo::DemoWindow> window;
		if (!opts.headless)
		{
			auto wndOpts   = demo::WindowOptions{};
			wndOpts.width  = static_cast<int>(opts.width);
			wndOpts.height = static_cast<int>(opts.height);
			wndOpts.title  = "Bernini bgl_async_compute";
			window.emplace(wndOpts);
		}

		auto ctxDesc             = bgpu::GpuContextDesc();
		ctxDesc.enableDebugLayer = true;
		ctxDesc.logLevel         = bgpu::LogLevel::kInfo;
		ctxDesc.shaderCacheDir   = "shadercache";
		auto context             = bgpu::CreateGpuContext(ctxDesc);

		auto graphics = bgl::CreateGraphics(context, bgl::GraphicsOptions{});
		auto compute  = crowd::CreateHashFillJob(context, { .count = opts.count });

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = static_cast<int>(opts.width);
		targetDesc.height   = static_cast<int>(opts.height);
		targetDesc.headless = opts.headless;
		targetDesc.wnd      = window ? window->NativeHandle() : nullptr;
		auto target         = graphics->CreateRenderTarget(targetDesc);

		auto scene    = graphics->CreateScene(bgl::SceneDesc());
		auto view     = graphics->CreateSceneView(scene, 8);
		auto material = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(0.8f, 0.3f, 0.2f, 1.0f), .roughnessFactor = 0.4f });
		auto cube = view->CreateStaticMeshInstance(scene->AddCubeGeom(material), glm::mat4(1.0f));

		auto camera = bgl::Camera();
		camera.LookAt(glm::vec3(3.0f, 2.5f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(opts.width) / static_cast<float>(opts.height),
				0.1f,
				100.0f);

		auto renderJob   = bgl::RenderJob{};
		renderJob.view   = view;
		renderJob.camera = camera;
		renderJob.viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		uint32_t seed          = 1;
		uint32_t results       = 0;
		uint32_t mismatches    = 0;
		uint32_t overlapFrames = 0;
		compute->Submit(seed);

		uint32_t frame = 0;
		for (; opts.frames == 0 || frame < opts.frames; ++frame)
		{
			if (window)
			{
				demo::PumpEvents();
				if (window->ShouldClose())
					break;
			}

			view->SetInstanceTransform(
				cube,
				glm::rotate(
					glm::mat4(1.0f),
					static_cast<float>(frame) * 0.02f,
					glm::vec3(0.0f, 1.0f, 0.0f)));

			const uint64_t completedBefore = compute->GetCompletedFence();
			graphics->DrawFrame(target, renderJob);
			const uint64_t completedAfter = compute->GetCompletedFence();

			const bool overlapped = completedBefore < compute->GetSubmittedFence();
			overlapFrames += overlapped ? 1u : 0u;

			std::cout << std::format(
				"frame {:>4}: compute fence {} submitted; {} completed before the draw, {} "
				"after{}\n",
				frame,
				compute->GetSubmittedFence(),
				completedBefore,
				completedAfter,
				overlapped ? " -- the frame was recorded while the kernel was in flight" : "");

			if (compute->InFlight())
				continue;

			const uint64_t gpu = Checksum(compute->GetReadback());
			const uint64_t cpu = ReferenceChecksum(compute->GetCount(), seed);
			++results;
			mismatches += gpu == cpu ? 0u : 1u;

			std::cout << std::format(
				"           readback seed {}: checksum {:016x}, CPU {:016x} -- {}\n",
				seed,
				gpu,
				cpu,
				gpu == cpu ? "match" : "MISMATCH");

			compute->Submit(++seed);
		}

		compute->Wait();
		graphics->WaitIdle();

		std::cout << std::format(
			"\n{} frames drawn, {} of them while the kernel ran on the compute queue; {} readbacks "
			"of "
			"{} elements, {} mismatched\n",
			frame,
			overlapFrames,
			results,
			compute->GetCount(),
			mismatches);

		return results > 0 && mismatches == 0 ? 0 : 1;
	}
}

int
main(int argc, char** argv)
{
	core::install_crash_handlers();

	auto opts = Options{};

	CLI::App app{ "Bernini bgl_async_compute example" };
	app.set_help_flag("--help", "Print this help message and exit");
	app.add_option("-w,--width", opts.width, "Window width in pixels")->check(CLI::PositiveNumber);
	app.add_option("-h,--height", opts.height, "Window height in pixels")
		->check(CLI::PositiveNumber);
	app.add_option("--frames", opts.frames, "Frames to draw before exiting; 0 runs until closed");
	app.add_option("--count", opts.count, "Elements the kernel fills per submission")
		->check(CLI::PositiveNumber);
	app.add_flag("--headless", opts.headless, "Render offscreen, with no window");

	CLI11_PARSE(app, argc, argv);

	try
	{
		return Run(opts);
	}
	catch (const std::exception& e)
	{
		std::cerr << "bgl_async_compute: " << e.what() << '\n';
		return 1;
	}
}
