#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/GpuContext.h>
#include <cmath>
#include <core/err/util.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ICrowd.h>
#include <crowdlib/debug/CrowdReadback.h>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// Groups of infantry and cavalry marching across a field, simulated by crowdlib on its compute queue
// and drawn by the renderer as one box per agent from the crowd's debug readback -- the only
// per-agent read the crowd has, and the one a game would never drive itself with. The crowd steps
// one fixed tick a frame while it can, and the frame draws whatever tick last completed. Partway
// through, a group splits off its rear and a cavalry group merges into another; the log prints
// every group's report as it goes.

namespace
{
	struct Options
	{
		uint32_t width  = 1280;
		uint32_t height = 720;

		// 0 runs until the window closes; otherwise every group must stand at its goal by then.
		uint32_t    frames   = 0;
		bool        headless = false;
		std::string screenshot;
	};

	constexpr float c_Tick = 1.0f / 30.0f;

	constexpr auto c_Infantry =
		crowd::AgentType{ .radius = 0.3f, .preferredSpeed = 1.2f, .maxSpeed = 1.5f, .mass = 80.0f };
	constexpr auto c_Cavalry = crowd::AgentType{ .radius         = 0.8f,
		                                         .preferredSpeed = 4.0f,
		                                         .maxSpeed       = 6.0f,
		                                         .mass           = 500.0f };

	struct Group
	{
		std::string        name;
		crowd::GroupHandle handle;
		uint32_t           type = 0;
		crowd::GroupOrders orders;
	};

	crowd::GroupOrders
	Orders(glm::vec2 goal, glm::vec2 facing, uint32_t frontage, float spacing)
	{
		return { .goal      = goal,
			     .facing    = facing,
			     .formation = { .frontage = frontage, .spacing = spacing },
			     .pace      = 1.0f };
	}

	const Group&
	Find(const std::vector<Group>& groups, crowd::GroupHandle handle)
	{
		for (const auto& group : groups)
		{
			if (group.handle == handle)
				return group;
		}
		core::fatal("a group the example never made");
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
			wndOpts.title  = "Bernini bgl_crowd";
			window.emplace(wndOpts);
		}

		auto ctxDesc             = bgpu::GpuContextDesc();
		ctxDesc.enableDebugLayer = true;
		ctxDesc.shaderCacheDir   = "shadercache";
		auto context             = bgpu::CreateGpuContext(ctxDesc);
		auto graphics            = bgl::CreateGraphics(context, bgl::GraphicsOptions{});

		auto crowdDesc               = crowd::CrowdDesc();
		crowdDesc.agentTypes         = { c_Infantry, c_Cavalry };
		crowdDesc.maxAgents          = 256;
		crowdDesc.maxGroups          = 8;
		crowdDesc.tickSeconds        = c_Tick;
		crowdDesc.debugAgentReadback = true;
		auto crowd                   = crowd::CreateCrowd(context, crowdDesc);

		// Each group spawns at its start in its march's formation, and gets its orders after the
		// first tick: orders given before then would be the ones it spawns in.
		auto       groups = std::vector<Group>();
		const auto add    = [&](std::string        name,
		                        uint32_t           type,
		                        uint32_t           count,
		                        glm::vec2          start,
		                        crowd::GroupOrders orders) {
			auto spawn = orders;
			spawn.goal = start;
			const auto handle =
				crowd->CreateGroup({ .agentType = type, .agentCount = count, .orders = spawn });
			groups.push_back(
				{ .name = std::move(name), .handle = handle, .type = type, .orders = orders });
		};
		add("left foot",
		    0,
		    60,
		    { -15.0f, -6.0f },
		    Orders({ -15.0f, 6.0f }, { 0.0f, 1.0f }, 10, 1.0f));
		add("right foot",
		    0,
		    40,
		    { 15.0f, 6.0f },
		    Orders({ 15.0f, -6.0f }, { 0.0f, -1.0f }, 8, 1.0f));
		add("north horse",
		    1,
		    20,
		    { -10.0f, 15.0f },
		    Orders({ 10.0f, 15.0f }, { 1.0f, 0.0f }, 5, 2.0f));
		add("south horse",
		    1,
		    16,
		    { 10.0f, -15.0f },
		    Orders({ -10.0f, -15.0f }, { -1.0f, 0.0f }, 4, 2.0f));
		crowd->Step();
		for (const auto& group : groups) crowd->SetOrders(group.handle, group.orders);

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = static_cast<int>(opts.width);
		targetDesc.height   = static_cast<int>(opts.height);
		targetDesc.headless = opts.headless;
		targetDesc.wnd      = window ? window->NativeHandle() : nullptr;
		auto target         = graphics->CreateRenderTarget(targetDesc);

		auto scene = graphics->CreateScene(bgl::SceneDesc());
		auto view  = graphics->CreateSceneView(scene, crowdDesc.maxAgents + 1);

		// The only light: with no environment map, a scene without a sun renders black.
		view->SetDirectionalLight(
			{ .direction = glm::vec3(-0.4f, -1.0f, -0.3f),
		      .color     = glm::vec3(1.0f),
		      .intensity = 3.0f });

		const auto ground = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(0.35f, 0.45f, 0.3f, 1.0f), .roughnessFactor = 0.9f });
		view->CreateStaticMeshInstance(
			scene->AddCubeGeom(ground),
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, 0.0f)) *
				glm::scale(glm::mat4(1.0f), glm::vec3(60.0f, 0.05f, 60.0f)));

		// One pool of boxes per type, as many as the type's agents: a split or a merge keeps a type's
		// count, so each frame hands a type's agents to its pool in readback order.
		const glm::vec4 colors[] = { glm::vec4(0.8f, 0.25f, 0.2f, 1.0f),
			                         glm::vec4(0.2f, 0.35f, 0.8f, 1.0f) };
		// Whole extents; AddCubeGeom's cube spans -1..1, so it is scaled by half of each.
		const glm::vec3 sizes[] = { glm::vec3(0.5f, 1.7f, 0.4f), glm::vec3(0.8f, 1.6f, 1.5f) };
		std::vector<bgl::MeshInstanceHandle> pools[2];
		uint32_t                             counts[2] = { 100, 36 };
		for (uint32_t type = 0; type < 2; ++type)
		{
			// Below the ground until an agent takes it: a degenerate matrix has no inverse for the
			// renderer to take.
			const auto parked = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f));
			const auto geom   = scene->AddCubeGeom(scene->CreatePbrMaterial(
				{ .baseColorFactor = colors[type], .roughnessFactor = 0.6f }));
			for (uint32_t i = 0; i < counts[type]; ++i)
				pools[type].push_back(view->CreateStaticMeshInstance(geom, parked));
		}

		auto camera = bgl::Camera();
		camera.LookAt(glm::vec3(0.0f, 38.0f, 30.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(50.0f),
				static_cast<float>(opts.width) / static_cast<float>(opts.height),
				0.1f,
				200.0f);

		auto renderJob   = bgl::RenderJob{};
		renderJob.view   = view;
		renderJob.camera = camera;
		renderJob.viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		uint64_t drawnTick = 0;
		uint32_t frame     = 0;
		for (; opts.frames == 0 || frame < opts.frames; ++frame)
		{
			if (window)
			{
				demo::PumpEvents();
				if (window->ShouldClose())
					break;
			}

			if (crowd->CanStep())
			{
				const uint64_t next = crowd->GetSubmittedTick() + 1;
				if (next == 60)
				{
					auto orders = Orders({ -6.0f, 0.0f }, { 1.0f, 0.0f }, 5, 1.0f);
					groups.push_back(
						{ .name   = "left rear",
					      .handle = crowd->SplitGroup(groups[0].handle, 20),
					      .type   = 0,
					      .orders = orders });
					crowd->SetOrders(groups.back().handle, orders);
					std::cout << std::format(
						"tick {}: left foot splits off its rear twenty\n",
						next);
				}
				if (next == 150)
				{
					crowd->MergeGroup(groups[3].handle, groups[2].handle);
					std::cout << std::format("tick {}: south horse joins north horse\n", next);
				}
				crowd->Step();
			}

			const auto readback = crowd->ReadDebugAgents();
			if (readback && readback->tick != drawnTick)
			{
				drawnTick        = readback->tick;
				uint32_t used[2] = { 0, 0 };
				for (const auto& range : readback->groups)
				{
					const auto type = Find(groups, range.group).type;
					for (uint32_t i = 0; i < range.count && used[type] < counts[type]; ++i)
					{
						const auto& agent = readback->agents[range.first + i];
						const auto  yaw   = std::atan2(agent.facing.x, agent.facing.y);
						view->SetInstanceTransform(
							pools[type][used[type]++],
							glm::translate(
								glm::mat4(1.0f),
								glm::vec3(
									agent.position.x,
									sizes[type].y * 0.5f,
									agent.position.y)) *
								glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
								glm::scale(glm::mat4(1.0f), sizes[type] * 0.5f));
					}
				}

				if (drawnTick % 60 == 0)
				{
					for (const auto& group : groups)
					{
						if (!crowd->HasGroup(group.handle))
							continue;
						if (const auto report = crowd->GetReport(group.handle))
						{
							std::cout << std::format(
								"tick {:>4}: {:<12} {:>3} agents at ({:6.2f}, {:6.2f})\n",
								report->tick,
								group.name,
								report->agentCount,
								report->meanPosition.x,
								report->meanPosition.y);
						}
					}
				}
			}

			graphics->DrawFrame(target, renderJob);
		}

		crowd->Wait();
		graphics->WaitIdle();
		if (!opts.screenshot.empty())
			graphics->ScreenshotPng(target, opts.screenshot);
		if (opts.frames == 0)
			return 0;

		// A formation's short rear rank moves its mean off the goal, but by less than one spacing.
		int failed = 0;
		for (const auto& group : groups)
		{
			if (!crowd->HasGroup(group.handle))
				continue;
			const auto report = crowd->GetReport(group.handle);
			const auto miss =
				report ? glm::length(report->meanPosition - group.orders.goal) : INFINITY;
			const bool arrived = miss < group.orders.formation.spacing;
			failed += arrived ? 0 : 1;
			std::cout << std::format(
				"{:<12} {} its goal ({:.3f} away)\n",
				group.name,
				arrived ? "reached" : "DID NOT REACH",
				miss);
		}
		std::cout << std::format("{} frames, {} ticks\n", frame, crowd->GetCompletedTick());
		return failed == 0 ? 0 : 1;
	}
}

int
main(int argc, char** argv)
{
	core::install_crash_handlers();

	auto opts = Options{};

	CLI::App app{ "Bernini bgl_crowd example" };
	app.set_help_flag("--help", "Print this help message and exit");
	app.add_option("-w,--width", opts.width, "Window width in pixels")->check(CLI::PositiveNumber);
	app.add_option("-h,--height", opts.height, "Window height in pixels")
		->check(CLI::PositiveNumber);
	app.add_option(
		"--frames",
		opts.frames,
		"Frames to draw, then exit non-zero unless every group stands at its goal; 0 runs until "
		"closed");
	app.add_flag("--headless", opts.headless, "Render offscreen, with no window");
	app.add_option("--screenshot", opts.screenshot, "Write the last frame drawn to this PNG");

	CLI11_PARSE(app, argc, argv);

	try
	{
		return Run(opts);
	}
	catch (const std::exception& e)
	{
		std::cerr << "bgl_crowd: " << e.what() << '\n';
		return 1;
	}
}
