#define NOMINMAX

#include <CLI/CLI.hpp>
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/PassHistory.h>
#include <bgl/PassTiming.h>
#include <bgl/RenderJob.h>
#include <bgl/SkyboxDesc.h>
#include <bgl/Viewport.h>
#include <bgl/bgl.h>
#include <bgl/glm.h>
#include <bgl/pass_timing_csv.h>
#include <bgl/types/EnvironmentMapDesc.h>
#include <bgl/types/SceneDesc.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <gamelib/AssetManager.h>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

		bool taa = true;

		// Set by the run rather than by the caller: a project with no environment renders unlit, and
		// what the passes cost is not the same question lit as it is unlit.
		bool lit = false;
	};

	struct PassCost
	{
		std::string name;
		double      median = 0.0;
		double      max    = 0.0;
		std::size_t frames = 0;
	};

	/**
	 * The middle and the worst of `samples`, which must not be empty. The median rather than the
	 * mean, because one frame that stalled moves a mean and the question here is what a frame costs
	 * rather than what the run took.
	 */
	[[nodiscard]] PassCost
	Summarise(std::string name, std::vector<double> samples)
	{
		std::ranges::sort(samples);
		return PassCost{ .name   = std::move(name),
			             .median = samples[samples.size() / 2],
			             .max    = samples.back(),
			             .frames = samples.size() };
	}

	[[nodiscard]] std::vector<PassCost>
	CostliestFirst(const bgl::PassHistory& history)
	{
		std::vector<PassCost> costs;
		std::vector<double>   totals;

		for (std::size_t pass = 0; pass < history.Passes().size(); ++pass)
		{
			std::vector<double> samples;
			for (std::size_t sample = 0; sample < history.SampleCount(); ++sample)
			{
				// A pass the frame did not run is not a zero-cost frame for it: the frame graph
				// culled it, and averaging in a zero would report a pass as cheaper than it is.
				if (const std::optional<double> cell = history.At(sample, pass))
					samples.push_back(*cell);
			}

			if (!samples.empty())
				costs.push_back(Summarise(std::string(history.Passes()[pass]), std::move(samples)));
		}

		std::ranges::sort(costs, [](const PassCost& a, const PassCost& b) {
			return a.median > b.median;
		});

		for (std::size_t sample = 0; sample < history.SampleCount(); ++sample)
		{
			totals.push_back(history.TotalAt(sample));
		}
		if (!totals.empty())
			costs.push_back(Summarise("frame", std::move(totals)));

		return costs;
	}

	void
	Report(const bgl::PassHistory& history, const Options& opts)
	{
		const std::vector<PassCost> costs = CostliestFirst(history);

		std::size_t width = 4;
		for (const PassCost& cost : costs)
		{
			width = std::max(width, cost.name.size());
		}

		std::cout << std::format(
			"\n{} frames of {} at {}x{}, {}, TAA {}, {} dropped to warm-up\n\n",
			history.SampleCount(),
			opts.mesh,
			opts.width,
			opts.height,
			opts.lit ? "lit" : "unlit",
			opts.taa ? "on" : "off",
			opts.warmup);

		std::cout << std::format(
			"{:<{}}  {:>10}  {:>10}  {:>7}\n",
			"pass",
			width,
			"median ms",
			"max ms",
			"frames");
		std::cout << std::string(width + 33, '-') << '\n';

		for (const PassCost& cost : costs)
		{
			// The frame total is the sum of the bands, not a pass, so it is ruled off from them.
			if (cost.name == "frame")
				std::cout << std::string(width + 33, '-') << '\n';

			std::cout << std::format(
				"{:<{}}  {:>10.3f}  {:>10.3f}  {:>7}\n",
				cost.name,
				width,
				cost.median,
				cost.max,
				cost.frames);
		}
	}

	[[nodiscard]] glm::mat4
	WorldTransform(const assetlib::BMesh& mesh, uint32_t node)
	{
		auto world = glm::mat4(1.0f);
		for (uint32_t n = node; n != assetlib::c_InvalidIndex; n = mesh.nodes[n].parent)
		{
			world = assetlib::toMatrix(mesh.nodes[n].localTransform) * world;
		}
		return world;
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

		CLI11_PARSE(app, argc, argv);
	}

	const auto dataRoot = std::filesystem::path(opts.project);

	auto gfxOpts           = bgl::GraphicsOptions();
	gfxOpts.logLevel       = bgl::GraphicsOptions::LogLevel::kError;
	gfxOpts.shaderCacheDir = "shadercache";
	gfxOpts.maxTextures    = 512;
	gfxOpts.maxSrvs        = 1024;
	gfxOpts.maxCbvSrvUavs  = 4096;
	auto graphics          = bgl::CreateGraphics(gfxOpts);

	auto targetDesc       = bgl::RenderTargetDesc();
	targetDesc.width      = static_cast<int>(opts.width);
	targetDesc.height     = static_cast<int>(opts.height);
	targetDesc.headless   = true;
	targetDesc.taaEnabled = opts.taa;
	auto target           = graphics->CreateRenderTarget(targetDesc);

	// Every frame from here on is timed, which is what the whole run is for.
	target->SetGpuTimingEnabled(true);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 128;
	sceneDesc.initialMeshlets             = 65536;
	sceneDesc.initialSubmeshes            = 512;
	sceneDesc.initialVertexBufferByteSize = 64u << 20;
	sceneDesc.initialIndices              = 4000000;
	sceneDesc.initialPbrMaterials         = 128;
	sceneDesc.initialLoosePbrMaterials    = 128;

	auto scene     = graphics->CreateScene(std::move(sceneDesc));
	auto view      = graphics->CreateSceneView(scene, 128);
	auto assets    = game::AssetManager(scene, dataRoot);
	auto envAssets = game::AssetManager(
		scene,
		opts.envRoot.empty() ? dataRoot : std::filesystem::path(opts.envRoot));

	// A project need not have one, and the tool is worth more reporting an unlit cost than refusing
	// to measure -- so this is a warning rather than a failure, and the report carries it.
	try
	{
		const auto env = envAssets.AcquireEnvironment(opts.env);
		opts.lit       = env.HasLighting();
		if (opts.lit)
		{
			view->SetEnvironmentMap({ env.irradiance, env.prefilter });
			view->SetExposure(env.exposure);
		}
		if (env.HasSky())
			view->SetSkyBox(bgl::SkyboxDesc{ env.skybox, env.skyMipLevel, 1.0f, env.skyRotationY });
	}
	catch (const std::exception& e)
	{
		std::cerr << std::format("{}: {}\nRendering unlit.\n", opts.env, e.what());
	}

	const auto model = assetlib::AssetStore(dataRoot).Load<assetlib::BMesh>(opts.mesh);

	auto boundsMin = glm::vec3((std::numeric_limits<float>::max)());
	auto boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
	for (uint32_t n = 0; n < model.nodes.size(); ++n)
	{
		if (model.nodes[n].mesh == assetlib::c_InvalidIndex)
			continue;

		// A skinned submesh's positions are already in rig space, which full weight on the bone
		// undoes -- applying the node's transform again would place it twice.
		const glm::mat4 world = assetlib::isSkinned(model, model.nodes[n].mesh) ?
		                            glm::mat4(1.0f) :
		                            WorldTransform(model, n);
		assets.CreateInstance(view, assets.AcquireMesh(opts.mesh, model.nodes[n].mesh), world);

		const assetlib::Mesh& entry = model.meshes[model.nodes[n].mesh];
		for (uint32_t s = 0; s < entry.submeshCount; ++s)
		{
			const assetlib::Submesh& sub = model.submeshes[entry.firstSubmesh + s];
			for (int corner = 0; corner < 8; ++corner)
			{
				const auto local = glm::vec3(
					(corner & 1) ? sub.aabbMax.x : sub.aabbMin.x,
					(corner & 2) ? sub.aabbMax.y : sub.aabbMin.y,
					(corner & 4) ? sub.aabbMax.z : sub.aabbMin.z);
				const auto worldPos = glm::vec3(world * glm::vec4(local, 1.0f));
				boundsMin           = glm::min(boundsMin, worldPos);
				boundsMax           = glm::max(boundsMax, worldPos);
			}
		}
	}

	// Framed on its own bounding sphere, so the same model fills the frame the same way whatever
	// scale it was authored at -- a cost read at one framing says nothing about a cost read at
	// another, and a fixed camera would frame one project's model and miss the next one's.
	const glm::vec3 centre = (boundsMin + boundsMax) * 0.5f;
	const float     radius = glm::max(glm::length(boundsMax - centre), 0.001f);
	const float     fovY   = glm::radians(60.0f);
	const float     dist   = radius / glm::tan(fovY * 0.5f) * 1.4f;
	const float     aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);

	auto camera = bgl::Camera();
	camera
		.LookAt(
			centre + glm::normalize(glm::vec3(0.4f, 0.35f, 1.0f)) * dist,
			centre,
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(fovY, aspect, glm::max(radius * 0.02f, 0.01f), dist + radius * 4.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
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
