#include "SkinnedAgent.h"
#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/project_layout.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/InstanceDesc.h>
#include <bgl/types/PassTiming.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/GpuContext.h>
#include <chrono>
#include <cmath>
#include <core/err/util.h>
#include <crowd_render/CrowdInstanceBlocks.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ICrowd.h>
#include <crowdlib/debug/CrowdReadback.h>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <gamelib/AssetManager.h>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Groups of infantry and cavalry marching across a field, simulated by crowdlib on its compute queue
// and drawn by the renderer as one box per agent from the crowd's debug readback -- the only
// per-agent read the crowd has, and the one a game would never drive itself with. The frame draws
// whatever tick last completed. Partway through, a group splits off its rear and a cavalry group
// merges into another; the log prints each moving group's report as it goes. Agents do not yet keep
// out of each other's way -- the crowd only plans velocities so far -- so boxes overlap wherever
// groups cross.
//
// Run until closed, the crowd steps its fixed tick at the wall clock's pace and every group marches
// back and forth between its two ends. With --frames it steps one tick a frame and marches once,
// so the run is the same every time and its end can be checked. --units multiplies the crowd, and
// the log splits each frame's time into the crowd, posing a box per agent, and the renderer, and
// gives the crowd's mean GPU time a tick.
//
// With --handoff the crowd is drawn GPU to GPU instead (crowd_render::CrowdInstanceBlocks): no
// readback and no posing, interpolated between ticks. With --project and --import too, every agent
// is that import's skinned character looping --clip, each a phase of its own, posed per instance
// near the camera and drawn from its rig's table far from it.

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

		// Every group holds this many times its agents, and the field grows by its square root so
		// the blocks keep their shape and do not start on top of each other.
		uint32_t units = 1;

		// Times every pass on the GPU and prints each one's mean. Off by default: a timed frame
		// costs more on Metal, so the frame split is read from a run without it.
		bool passTimings = false;

		// Draws the crowd from its render ring rather than its debug readback.
		bool handoff = false;

		// A project's data root and an import in it: every agent is drawn as that skinned
		// character, playing `clip` (its first when empty). Needs the handoff.
		std::string project;
		std::string importKey;
		std::string clip;
	};

	constexpr float c_Tick = 1.0f / 30.0f;

	// Long enough for the slowest group to cross the field and stand before it turns around.
	constexpr uint64_t c_MarchTicks = 540;

	// The four groups' agents at --units 1.
	constexpr uint32_t c_BaseAgents = 60 + 40 + 20 + 16;

	// The ring a handoff needs, with two ticks of slack before a slow frame holds the crowd back.
	constexpr uint32_t c_RenderRingSlack = 2;

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

		// The other end of its march, where it turns to next.
		glm::vec2 home = glm::vec2(0.0f);
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
		// A character's materials may shade through surfaces its project authors, which the
		// renderer registers only from this directory, and only when it is created.
		if (!opts.project.empty())
		{
			const auto surfaceDir =
				std::filesystem::path(opts.project) / assetlib::c_ShadersDirectoryName;
			if (std::filesystem::is_directory(surfaceDir))
				ctxDesc.clientShaderDir = surfaceDir;
		}
		auto context  = bgpu::CreateGpuContext(ctxDesc);
		auto graphics = bgl::CreateGraphics(context, bgl::GraphicsOptions{});

		const uint32_t units = opts.units;
		const float    scale = std::sqrt(static_cast<float>(units));
		const auto     widen = std::max(1u, static_cast<uint32_t>(std::lround(scale)));
		const auto     at    = [scale](float x, float z) { return glm::vec2(x, z) * scale; };
		// A march is longer by the field's growth, so its time is too.
		const auto marchTicks = static_cast<uint64_t>(static_cast<float>(c_MarchTicks) * scale);

		auto crowdDesc       = crowd::CrowdDesc();
		crowdDesc.agentTypes = { c_Infantry, c_Cavalry };
		// Exactly the agents the example makes: a handoff's blocks draw and cull every one of
		// maxAgents slots per type, live or not.
		crowdDesc.maxAgents          = c_BaseAgents * opts.units;
		crowdDesc.maxGroups          = 8;
		crowdDesc.tickSeconds        = c_Tick;
		crowdDesc.debugAgentReadback = !opts.handoff;
		crowdDesc.renderRingTicks =
			opts.handoff ? crowdDesc.maxTicksInFlight + 3 + c_RenderRingSlack : 0;
		auto crowd = crowd::CreateCrowd(context, crowdDesc);

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
				{ .name   = std::move(name),
			      .handle = handle,
			      .type   = type,
			      .orders = orders,
			      .home   = start });
		};
		add("left foot",
		    0,
		    60 * units,
		    at(-15.0f, -6.0f),
		    Orders(at(-15.0f, 6.0f), { 0.0f, 1.0f }, 10 * widen, 1.0f));
		add("right foot",
		    0,
		    40 * units,
		    at(15.0f, 6.0f),
		    Orders(at(15.0f, -6.0f), { 0.0f, -1.0f }, 8 * widen, 1.0f));
		add("north horse",
		    1,
		    20 * units,
		    at(-10.0f, 15.0f),
		    Orders(at(10.0f, 15.0f), { 1.0f, 0.0f }, 5 * widen, 2.0f));
		add("south horse",
		    1,
		    16 * units,
		    at(10.0f, -15.0f),
		    Orders(at(-10.0f, -15.0f), { -1.0f, 0.0f }, 4 * widen, 2.0f));
		crowd->Step();
		for (const auto& group : groups) crowd->SetOrders(group.handle, group.orders);

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = static_cast<int>(opts.width);
		targetDesc.height   = static_cast<int>(opts.height);
		targetDesc.headless = opts.headless;
		targetDesc.wnd      = window ? window->NativeHandle() : nullptr;
		auto target         = graphics->CreateRenderTarget(targetDesc);
		target->SetGpuTimingEnabled(opts.passTimings);

		auto scene = graphics->CreateScene(bgl::SceneDesc());
		auto view  = graphics->CreateSceneView(scene, 2 * crowdDesc.maxAgents + 1);

		// Before the blocks, so the geoms they name outlive them.
		std::optional<assetlib::AssetStore>        store;
		std::optional<game::AssetManager>          assets;
		std::optional<crowd_example::SkinnedAgent> character;
		if (!opts.importKey.empty())
		{
			const auto dataRoot = std::filesystem::path(opts.project);
			store.emplace(dataRoot);
			assets.emplace(scene, dataRoot);
			character = crowd_example::LoadSkinnedAgent(
				*store,
				dataRoot,
				*assets,
				opts.importKey,
				opts.clip);
			std::cout << std::format(
				"every agent is {}, {} geom(s), looping clip {} over {:.2f} s\n",
				opts.importKey,
				character->geoms.size(),
				character->clipIndex,
				character->cycleSeconds);
		}

		// The only light: with no environment map, a scene without a sun renders black.
		view->SetDirectionalLight(
			{ .direction = glm::vec3(-0.4f, -1.0f, -0.3f),
		      .color     = glm::vec3(1.0f),
		      .intensity = 3.0f });

		const auto ground = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(0.35f, 0.45f, 0.3f, 1.0f), .roughnessFactor = 0.9f });
		view->CreateStaticMeshInstance(
			bgl::StaticMeshInstanceDesc()
				.SetGeom(scene->AddCubeGeom(ground))
				.SetTransform(
					glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, 0.0f)) *
					glm::scale(glm::mat4(1.0f), glm::vec3(60.0f * scale, 0.05f, 60.0f * scale))));

		// One pool of boxes per type, as many as the type's agents: a split or a merge keeps a type's
		// count, so each frame hands a type's agents to its pool in readback order.
		const glm::vec4 colors[] = { glm::vec4(0.8f, 0.25f, 0.2f, 1.0f),
			                         glm::vec4(0.2f, 0.35f, 0.8f, 1.0f) };
		// Whole extents; AddCubeGeom's cube spans -1..1, so it is scaled by half of each.
		const glm::vec3 sizes[] = { glm::vec3(0.5f, 1.7f, 0.4f), glm::vec3(0.8f, 1.6f, 1.5f) };
		std::vector<bgl::MeshInstanceHandle> pools[2];
		uint32_t                             counts[2] = { 100 * units, 36 * units };
		auto                                 handoffDesc =
			crowd_render::CrowdInstanceBlocksDesc().SetCrowd(crowd).SetGraphics(graphics).SetView(
				view);
		for (uint32_t type = 0; type < 2; ++type)
		{
			if (character)
			{
				// Standing on the ground at the type's height, whatever the character's own scale.
				const float height = character->bounds.max.y - character->bounds.min.y;
				const float fit    = sizes[type].y / height;
				auto        mesh =
					crowd_render::AgentTypeMeshDesc()
						.SetCapacity(counts[type])
						.SetPlayback(bgl::SkinnedPlaybackDesc::FromClip(character->clipIndex))
						.SetPhaseSpreadSeconds(character->cycleSeconds)
						.SetModel(
							glm::translate(
								glm::mat4(1.0f),
								glm::vec3(0.0f, -character->bounds.min.y * fit, 0.0f)) *
							glm::scale(glm::mat4(1.0f), glm::vec3(fit)) * character->world);
				for (const bgl::GeomHandle skinned : character->geoms) mesh.AddGeom(skinned);
				handoffDesc.AddType(std::move(mesh));
				continue;
			}
			const auto geom = scene->AddCubeGeom(scene->CreatePbrMaterial(
				{ .baseColorFactor = colors[type], .roughnessFactor = 0.6f }));
			if (opts.handoff)
			{
				handoffDesc.AddType(
					crowd_render::AgentTypeMeshDesc()
						.AddGeom(geom)
						.SetCapacity(counts[type])
						.SetModel(
							glm::translate(
								glm::mat4(1.0f),
								glm::vec3(0.0f, sizes[type].y * 0.5f, 0.0f)) *
							glm::scale(glm::mat4(1.0f), sizes[type] * 0.5f)));
				continue;
			}

			// Below the ground until an agent takes it: a degenerate matrix has no inverse for the
			// renderer to take.
			const auto parked = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f));
			for (uint32_t i = 0; i < counts[type]; ++i)
				pools[type].push_back(view->CreateStaticMeshInstance(
					bgl::StaticMeshInstanceDesc().SetGeom(geom).SetTransform(parked)));
		}
		std::optional<crowd_render::CrowdInstanceBlocks> handoff;
		if (opts.handoff)
			handoff.emplace(std::move(handoffDesc));

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 38.0f, 30.0f) * scale,
				glm::vec3(0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(50.0f),
				static_cast<float>(opts.width) / static_cast<float>(opts.height),
				0.1f,
				200.0f * scale);

		auto renderJob   = bgl::RenderJob{};
		renderJob.view   = view;
		renderJob.camera = camera;
		renderJob.viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		const bool live      = opts.frames == 0;
		auto       lastMeans = std::vector<glm::vec2>(groups.size() + 1, glm::vec2(INFINITY));
		auto       clock     = std::chrono::steady_clock::now();
		float      owed      = 0.0f;

		const auto started    = std::chrono::steady_clock::now();
		auto       spanStart  = started;
		uint32_t   spanFrames = 0;

		// Where a frame's time goes: stepping the crowd and reading it back, posing a box per agent
		// on the CPU, and the renderer's frame.
		using Clock          = std::chrono::steady_clock;
		double     crowdTime = 0.0;
		double     poseTime  = 0.0;
		double     drawTime  = 0.0;
		const auto since     = [](Clock::time_point from) {
			return std::chrono::duration<double>(Clock::now() - from).count();
		};

		// Each pass's summed GPU milliseconds and sample count, in the order passes first ran.
		std::vector<std::string>                 passNames;
		std::vector<std::pair<double, uint32_t>> passTotals;
		uint64_t                                 lastTimedFrame = 0;
		const auto                               collectTimings = [&] {
			const bgl::PassTimings timings = graphics->GetPassTimings(target);
			if (timings.passes.empty() || timings.frame == lastTimedFrame)
				return;
			lastTimedFrame = timings.frame;
			for (const auto& pass : timings.passes)
			{
				const auto found = std::ranges::find(passNames, pass.name);
				const auto index = static_cast<size_t>(found - passNames.begin());
				if (found == passNames.end())
				{
					passNames.push_back(pass.name);
					passTotals.emplace_back(0.0, 0u);
				}
				passTotals[index].first += pass.milliseconds;
				++passTotals[index].second;
			}
		};

		// Each completed tick's GPU time, read while the crowd still holds it.
		double     tickGpuTime  = 0.0;
		uint32_t   tickGpuCount = 0;
		uint64_t   timedTick    = 0;
		const auto collectTicks = [&] {
			for (const uint64_t completed = crowd->GetCompletedTick(); timedTick < completed;)
			{
				if (const auto ms = crowd->GetTickGpuMilliseconds(++timedTick))
				{
					tickGpuTime += *ms;
					++tickGpuCount;
				}
			}
		};

		uint64_t drawnTick = 0;
		uint32_t frame     = 0;
		for (; live || frame < opts.frames; ++frame)
		{
			if (window)
			{
				demo::PumpEvents();
				if (window->ShouldClose())
					break;
			}

			// Live, a tick is owed per c_Tick of wall time; capped, so a stall does not replay as a
			// burst.
			const auto now = std::chrono::steady_clock::now();
			owed =
				std::min(owed + std::chrono::duration<float>(now - clock).count(), 4.0f * c_Tick);
			clock                 = now;
			const bool due        = !live || owed >= c_Tick;
			const auto crowdStart = Clock::now();

			if (due && crowd->CanStep())
			{
				owed -= live ? c_Tick : 0.0f;
				const uint64_t next = crowd->GetSubmittedTick() + 1;
				if (live && next % marchTicks == 0)
				{
					for (auto& group : groups)
					{
						if (!crowd->HasGroup(group.handle))
							continue;
						std::swap(group.orders.goal, group.home);
						group.orders.facing = -group.orders.facing;
						crowd->SetOrders(group.handle, group.orders);
					}
					std::cout << std::format("tick {}: every group turns around\n", next);
				}
				if (next == 60)
				{
					auto orders = Orders(at(-6.0f, 0.0f), { 1.0f, 0.0f }, 5 * widen, 1.0f);
					groups.push_back(
						{ .name   = "left rear",
					      .handle = crowd->SplitGroup(groups[0].handle, 20 * units),
					      .type   = 0,
					      .orders = orders,
					      .home   = at(-15.0f, -3.0f) });
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

			const auto readback = opts.handoff ? std::nullopt : crowd->ReadDebugAgents();
			crowdTime += since(crowdStart);
			collectTicks();
			const auto poseStart = Clock::now();
			if (handoff)
			{
				// Live, the frame is the owed fraction of a tick past the tick before the latest;
				// a tick a frame, it is the latest.
				handoff->PrepareFrame(live ? std::clamp(owed / c_Tick, 0.0f, 1.0f) : 1.0f);
			}
			const uint64_t completed =
				opts.handoff ? crowd->GetCompletedTick() : (readback ? readback->tick : drawnTick);
			if (completed != drawnTick)
			{
				drawnTick        = completed;
				uint32_t used[2] = { 0, 0 };
				for (const auto& range :
				     readback ? readback->groups : std::span<const crowd::debug::GroupAgents>())
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
					for (uint32_t i = 0; i < groups.size(); ++i)
					{
						const auto& group = groups[i];
						if (!crowd->HasGroup(group.handle))
							continue;
						const auto report = crowd->GetReport(group.handle);
						if (report && glm::length(report->meanPosition - lastMeans[i]) > 1e-3f)
						{
							lastMeans[i] = report->meanPosition;
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

			poseTime += since(poseStart);
			const auto drawStart = Clock::now();
			// The animation clock: the wall's live, a tick a frame otherwise, so a capped run poses
			// the same every time.
			renderJob.time =
				live ? static_cast<float>(since(started)) : static_cast<float>(frame) * c_Tick;
			graphics->DrawFrame(target, renderJob);
			if (handoff)
				handoff->FinishFrame();
			drawTime += since(drawStart);
			if (opts.passTimings)
				collectTimings();

			// Frame time over each few seconds: what --units is for.
			if (++spanFrames == 300)
			{
				const auto spanEnd = std::chrono::steady_clock::now();
				const auto seconds = std::chrono::duration<double>(spanEnd - spanStart).count();
				std::cout << std::format(
					"{} agents: {:.2f} ms a frame over the last {} frames\n",
					c_BaseAgents * units,
					1000.0 * seconds / spanFrames,
					spanFrames);
				spanStart  = spanEnd;
				spanFrames = 0;
			}
		}

		crowd->Wait();
		graphics->WaitIdle();
		collectTicks();
		if (opts.passTimings)
		{
			collectTimings();
			for (size_t i = 0; i < passNames.size(); ++i)
			{
				std::cout << std::format(
					"pass {:<32} {:8.3f} ms a frame over {} frames\n",
					passNames[i],
					passTotals[i].first / passTotals[i].second,
					passTotals[i].second);
			}
		}
		if (!opts.screenshot.empty())
			graphics->ScreenshotPng(target, opts.screenshot);

		const auto seconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
		std::cout << std::format(
			"{} agents, {} frames, {} ticks in {:.2f} s: {:.2f} ms a frame -- crowd {:.2f}, posing "
			"{:.2f}, drawing {:.2f}; crowd GPU {:.3f} ms a tick\n",
			c_BaseAgents * units,
			frame,
			crowd->GetCompletedTick(),
			seconds,
			frame > 0 ? 1000.0 * seconds / frame : 0.0,
			frame > 0 ? 1000.0 * crowdTime / frame : 0.0,
			frame > 0 ? 1000.0 * poseTime / frame : 0.0,
			frame > 0 ? 1000.0 * drawTime / frame : 0.0,
			tickGpuCount > 0 ? tickGpuTime / tickGpuCount : 0.0);
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
		return failed == 0 ? 0 : 1;
	}
}

int
main(int argc, char** argv)
{
	core::install_crash_handlers();
	// The log is read while the crowd runs, so every line reaches it as it is written.
	std::cout << std::unitbuf;

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
	app.add_flag(
		"--pass-timings",
		opts.passTimings,
		"Time every pass on the GPU and print each one's mean per frame");
	app.add_flag(
		"--handoff",
		opts.handoff,
		"Draw the crowd GPU to GPU from its render ring, with no readback or posing");
	app.add_option("--units", opts.units, "Multiply every group's agents by this; 1 is 136 agents")
		->check(CLI::PositiveNumber);
	auto* project = app.add_option(
		"--project",
		opts.project,
		"A project's data root, which --import is read from");
	auto* import = app.add_option(
		"--import",
		opts.importKey,
		"A .bimport or .glb in --project whose skinned character every agent is drawn as");
	app.add_option("--clip", opts.clip, "The clip the character loops; its first when omitted");
	import->needs(project);
	import->needs(app.get_option("--handoff"));
	project->needs(import);

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
