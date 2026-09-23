#define NOMINMAX

#include <CLI/CLI.hpp>
#include <assetlib/AssetStore.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Node.h>
#include <bgl/IGraphics.h>
#include <bgl/PassHistory.h>
#include <bgl/PassTiming.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <bgl/pass_timing_csv.h>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <gamelib/AssetManager.h>
#include <headless/PassCosts.h>
#include <headless/framing.h>
#include <headless/headless_render.h>
#include <iostream>
#include <string>

// What a model costs per frame graph pass, headlessly, against a project you name: the numbers the
// editor's GPU Timing Graph shows, taken where there is nobody at a keyboard. The artifact is the
// CSV -- frames down, passes across -- and stdout is the readout for whoever ran it.

namespace
{
	struct Options
	{
		std::string project = "assets/Data";
		std::string mesh    = "Derived/Meshes/apples.bmesh";
		std::string env     = "Authored/Environments/forest.benv";

		// Its own root: a project is free to have no environment of its own -- the test project has
		// none -- and lighting it from elsewhere is what keeps its cost representative.
		std::string envRoot;
		std::string out = "gpu_timings.csv";

		// Off by default: the artifact is the numbers. It is here because nothing else says what was
		// in frame, and a model lit by an environment the project does not have renders black at
		// full cost -- a number that is real and answers the wrong question.
		std::string png;
		uint32_t    width  = 1280;
		uint32_t    height = 720;
		uint32_t    frames = 60;

		// The opening frames of a fresh device pay for pipelines, uploads and a TAA history that has
		// nothing in it yet, and none of that is what the model costs.
		uint32_t warmup = 8;

		bool  taa          = true;
		float renderScale  = 1.0f;
		float taaSharpness = 0.0f;

		// Set by the run rather than by the caller: a project with no environment renders unlit, and
		// what the passes cost is not the same question lit as it is unlit.
		bool lit = false;
	};

	void
	Report(const bgl::PassHistory& history, const Options& opts)
	{
		std::cout << std::format(
			"\n{} frames of {} at {}x{}, render scale {}, {}, TAA {}, sharpness {}, {} dropped to "
			"warm-up\n\n",
			history.SampleCount(),
			opts.mesh,
			opts.width,
			opts.height,
			opts.renderScale,
			opts.lit ? "lit" : "unlit",
			opts.taa ? "on" : "off",
			opts.taaSharpness,
			opts.warmup);

		headless::PrintPassCosts(std::cout, headless::SummarisePasses(history));
	}
}

int
main(int argc, char** argv)
try
{
	Options opts;
	{
		CLI::App app{ "Per-pass GPU cost of a model, headless" };
		app.set_help_flag("--help", "Print this help message and exit");
		app.add_option(
			"--project",
			opts.project,
			"The project's Data directory: every key below is relative to it");
		app.add_option("--mesh", opts.mesh, "The .bmesh to render, keyed under --project");
		app.add_option("--env", opts.env, "The .benv to light it with");
		app.add_option(
			"--env-root",
			opts.envRoot,
			"Data root the .benv is keyed under; defaults to --project. A project with no "
			"environment of its own renders unlit, which the report says");
		app.add_option("--out", opts.out, "Where the CSV of every kept frame is written");
		app.add_option(
			"--png",
			opts.png,
			"Also write the last frame here, to check what was framed");
		app.add_option("--frames", opts.frames, "Frames to keep")->check(CLI::PositiveNumber);
		app.add_option("--warmup", opts.warmup, "Frames to render and discard first")
			->check(CLI::NonNegativeNumber);
		app.add_option("-w,--width", opts.width, "Render width")->check(CLI::PositiveNumber);
		app.add_option("-h,--height", opts.height, "Render height")->check(CLI::PositiveNumber);
		app.add_option("--taa", opts.taa, "Render with temporal antialiasing, as a viewport does");
		app.add_option(
			   "--render-scale",
			   opts.renderScale,
			   "The geometry passes' grid relative to the output size; below 1 the TAA resolve "
			   "reconstructs the output (RenderTargetDesc::renderScale)")
			->check(CLI::PositiveNumber);
		app.add_option(
			   "--taa-sharpness",
			   opts.taaSharpness,
			   "The sharpen applied to the resolved image, 0 (off) to 1; needs --taa "
			   "(RenderTargetDesc::taaSharpness)")
			->check(CLI::Range(0.0f, 1.0f));

		CLI11_PARSE(app, argc, argv);
	}

	const auto dataRoot = std::filesystem::path(opts.project);

	auto graphics = headless::CreateHeadlessGraphics(dataRoot);
	auto target   = headless::CreateHeadlessTarget(
		graphics,
		opts.width,
		opts.height,
		opts.taa,
		opts.renderScale);

	target->SetTaaSharpness(opts.taaSharpness);

	// Every frame from here on is timed, which is what the whole run is for.
	target->SetGpuTimingEnabled(true);

	auto scene     = headless::CreateHeadlessScene(graphics);
	auto view      = graphics->CreateSceneView(scene, 128);
	auto assets    = game::AssetManager(scene, dataRoot);
	auto envAssets = game::AssetManager(
		scene,
		opts.envRoot.empty() ? dataRoot : std::filesystem::path(opts.envRoot));

	opts.lit = headless::LightView(view, envAssets, opts.env);

	const auto model  = assetlib::AssetStore(dataRoot).Load<assetlib::BMesh>(opts.mesh);
	auto       bounds = headless::EmptyBounds();
	for (uint32_t n = 0; n < model.nodes.size(); ++n)
	{
		const uint32_t meshIndex = model.nodes[n].mesh;
		if (meshIndex == assetlib::c_InvalidIndex)
			continue;

		const glm::mat4 world = headless::InstanceTransform(model, n);
		assets.CreateInstance(view, assets.AcquireMesh(opts.mesh, meshIndex), world);
		headless::GrowBounds(bounds, world, headless::MeshEntryBounds(model, meshIndex));
	}

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = headless::FrameBounds(bounds, opts.width, opts.height);
	job.viewport = bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

	bgl::PassHistory history(opts.frames);
	uint64_t         lastFrame = 0;
	uint32_t         resolved  = 0;

	// A frame's samples land once its fence has passed, so a draw does not always produce a row --
	// hence a budget rather than a count. A device with no pass-boundary timestamp produces none at
	// all, which is what leaves the history empty below.
	const uint32_t budget = (opts.warmup + opts.frames) * 4 + 64;
	for (uint32_t drawn = 0; drawn < budget && history.SampleCount() < opts.frames; ++drawn)
	{
		graphics->DrawFrame(target, job);
		graphics->WaitIdle();

		const bgl::PassTimings timings = graphics->GetPassTimings(target);
		if (timings.passes.empty() || timings.frame == lastFrame)
			continue;

		lastFrame = timings.frame;
		if (++resolved > opts.warmup)
			history.Append(timings);
	}

	graphics->WaitIdle();

	if (!opts.png.empty())
		graphics->ScreenshotPng(target, opts.png);

	if (history.SampleCount() == 0)
	{
		std::cerr << "No pass timings resolved: this device cannot sample a timestamp at a pass "
					 "boundary.\n";
		return 1;
	}

	std::ofstream csv(opts.out, std::ios::binary | std::ios::trunc);
	if (!csv)
	{
		std::cerr << std::format("Could not write {}\n", opts.out);
		return 1;
	}
	csv << bgl::PassHistoryCsv(history);
	csv.close();

	Report(history, opts);
	std::cout << std::format("\n{} written\n", std::filesystem::absolute(opts.out).string());

	return 0;
}
catch (const std::exception& e)
{
	std::cerr << e.what() << '\n';
	return 1;
}
