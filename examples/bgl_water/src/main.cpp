#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <FlyCamera.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_messagebox.h>
#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/MeshInstanceHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/SkyboxDesc.h>  // IWYU pragma: keep
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/TerrainDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/GpuContext.h>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <gamelib/AssetManager.h>
#include <glm/gtc/matrix_transform.hpp>
#include <headless/headless_render.h>
#include <iostream>
#include <optional>
#include <string>
#include <terrainlib/Generate.h>
#include <terrainlib/height.h>

// A sea over generated hills, in a window: fly along the shore, and raise and lower the water to
// watch the foam follow it. The look is the project's water surface; the engine ships none.

namespace
{
	constexpr float c_LevelStep = 0.25f;

	// The unit cube, which spans [-1, 1], flattened to a slab a centimetre thick: its top face is
	// the sea, and its sides and bottom lie under it or face away and are culled.
	constexpr float c_SeaThickness = 0.01f;

	[[nodiscard]] glm::mat4
	SeaTransform(const float half, const float level)
	{
		return glm::scale(
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, level - 0.5f * c_SeaThickness, 0.0f)),
			glm::vec3(half, 0.5f * c_SeaThickness, half));
	}
}

int
main(int argc, char** argv)
{
	auto headless = false;

	try
	{
		uint32_t    width  = 1280;
		uint32_t    height = 720;
		uint32_t    frames = 90;
		uint32_t    seed   = 7;
		std::string project;
		std::string water = "Authored/Materials/Water/Lake.bmaterial";
		std::string shot  = "bgl_water.png";
		float       size  = 2000.0f;
		float       share = 0.55f;
		float       sun   = 2.0f;

		{
			CLI::App app{ "A sea of a project's water surface over generated hills" };
			app.set_help_flag("--help", "Print this help message and exit");
			app.add_option("-w,--width", width, "Width in pixels")->check(CLI::PositiveNumber);
			app.add_option("-h,--height", height, "Height in pixels")->check(CLI::PositiveNumber);
			app.add_option("--project", project, "The project's Data directory")->required();
			app.add_option(
				"--water",
				water,
				"The water surface .bmaterial, keyed against --project");
			app.add_option("--seed", seed, "The hills' seed");
			app.add_option("--size", size, "The field's side in metres")
				->check(CLI::PositiveNumber);
			app.add_option("--level", share, "The sea's height as a share of the hills' relief")
				->check(CLI::Range(0.0f, 1.0f));
			app.add_option("--sun", sun, "Sun intensity; 0 leaves only the environment")
				->check(CLI::NonNegativeNumber);
			app.add_flag("--headless", headless, "Render offscreen for --frames and exit");
			app.add_option("--frames", frames, "Frames to render in --headless mode")
				->check(CLI::PositiveNumber);
			app.add_option("--out", shot, "Where --headless writes its PNG");

			CLI11_PARSE(app, argc, argv);
		}

		const auto surfaceDir = std::filesystem::path(project) / "Authored" / "Shaders";
		if (!std::filesystem::is_directory(surfaceDir))
		{
			core::throw_runtime_error(
				"--project {} has no Authored/Shaders, so no water surface to draw",
				std::filesystem::absolute(project).string());
		}

		auto wnd = std::optional<demo::DemoWindow>();
		if (!headless)
		{
			auto opts         = demo::WindowOptions{};
			opts.width        = static_cast<int>(width);
			opts.height       = static_cast<int>(height);
			opts.title        = "Bernini bgl_water";
			opts.captureMouse = true;
			wnd.emplace(opts);
		}

		// The water surface is the project's, and surfaces are registered only at creation.
		auto ctxDesc             = bgpu::GpuContextDesc();
		ctxDesc.enableDebugLayer = true;
		ctxDesc.clientShaderDir  = surfaceDir;
		auto graphics =
			bgl::CreateGraphics(bgpu::CreateGpuContext(ctxDesc), bgl::GraphicsOptions{});

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = static_cast<int>(width);
		targetDesc.height   = static_cast<int>(height);
		targetDesc.headless = headless;
		targetDesc.wnd      = headless ? nullptr : wnd->NativeHandle();
		auto target         = graphics->CreateRenderTarget(targetDesc);

		auto scene  = graphics->CreateScene(bgl::SceneDesc());
		auto view   = graphics->CreateSceneView(scene, 16);
		auto assets = game::AssetManager(scene, project);

		// The environment ships with the engine's own fixture tree.
		auto       envAssets = game::AssetManager(scene, "assets/Data");
		const auto env       = envAssets.AcquireEnvironment("Authored/Environments/forest.benv");
		if (env.HasLighting())
		{
			view->SetEnvironmentMap({ env.irradiance, env.prefilter });
			view->SetExposure(env.exposure);
		}
		if (env.HasSky())
			view->SetSkyBox({ env.skybox, env.skyMipLevel, 1.0f, env.skyRotationY });

		view->SetDirectionalLight(
			{ .direction = headless::SunDirection(glm::radians(35.0f), glm::radians(38.0f)),
		      .color     = glm::vec3(1.0f, 0.96f, 0.88f),
		      .intensity = sun });

		constexpr float             c_Cell  = 2.0f;
		const auto                  samples = static_cast<uint32_t>(size / c_Cell) + 1;
		const assetlib::Heightfield field   = terrain::Generate(
			terrain::TerrainGenerateDesc()
				.SetShape(terrain::TerrainShape::kHilly)
				.SetSeed(seed)
				.SetSamples(samples, samples)
				.SetCellSize(c_Cell));

		// Centred on the origin with its lowest point at y = 0, so the level is a share of the relief.
		const float     half   = static_cast<float>(samples - 1) * c_Cell * 0.5f;
		const glm::vec3 origin = glm::vec3(-half, -field.minHeight, -half);
		(void)scene->CreateTerrain(
			bgl::TerrainDesc().SetHeightfield(&field).SetOrigin(origin).SetMaterial(
				scene->CreatePbrMaterial(
					{ .baseColorFactor = glm::vec4(0.3f, 0.42f, 0.18f, 1.0f),
		              .metallicFactor  = 0.0f,
		              .roughnessFactor = 1.0f })));

		float                         level = share * field.heightRange;
		const bgl::GeomHandle         sea   = scene->AddCubeGeom(assets.AcquireMaterial(water));
		const bgl::MeshInstanceHandle seaInstance = view->CreateStaticMeshInstance(
			bgl::StaticMeshInstanceDesc().SetGeom(sea).SetTransform(SeaTransform(half, level)));

		// High over the field's middle, looking out across the coast.
		constexpr float c_EyeAbove = 120.0f;
		const float     groundY    = terrain::HeightAt(field, origin, glm::vec2(0.0f, 0.0f));
		const float     eyeY       = std::max(groundY, level) + c_EyeAbove;

		const float aspect = static_cast<float>(width) / static_cast<float>(height);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, eyeY, 0.0f),
				glm::vec3(0.0f, level, -0.25f * half),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.1f, 4.0f * half);

		auto job     = bgl::RenderJob{};
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(width), static_cast<float>(height));

		if (headless)
		{
			// A fixed step, so the run is the same every time.
			constexpr float c_Step = 1.0f / 60.0f;

			for (uint32_t frame = 0; frame < frames; ++frame)
			{
				job.time = static_cast<float>(frame) * c_Step;
				graphics->DrawFrame(target, job);
			}

			graphics->ScreenshotPng(target, shot);
			std::cout << "wrote " << shot << '\n';
			return 0;
		}

		std::cout << std::format(
			"{} over a {:.0f} m field of {:.1f} m relief, the sea at {:.1f} m.\n"
			"WASD to fly, hold Shift and move the mouse to look.\n"
			"  [ ]    lower/raise the sea\n\n",
			water,
			size,
			field.heightRange,
			level);

		auto  clock   = demo::DeltaClock{};
		float elapsed = 0.0f;

		while (!wnd->ShouldClose())
		{
			auto moved = false;

			demo::PumpEvents([&level, &moved](const SDL_Event& ev) {
				if (ev.type != SDL_EVENT_KEY_DOWN)
					return;

				if (ev.key.key == SDLK_LEFTBRACKET)
				{
					level -= c_LevelStep;
					moved = true;
				}
				else if (ev.key.key == SDLK_RIGHTBRACKET)
				{
					level += c_LevelStep;
					moved = true;
				}
			});

			if (moved)
			{
				view->SetInstanceTransform(seaInstance, SeaTransform(half, level));
				std::cout << std::format("sea at {:.2f} m\n", level);
			}

			const float dt = clock.Tick();
			elapsed += dt;

			if (demo::ApplyFlyCam(camera, dt))
				job.camera = camera;

			job.time = elapsed;
			graphics->DrawFrame(target, job);
		}

		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "bgl_water: " << e.what() << '\n';

		if (!headless)
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "bgl_water", e.what(), nullptr);

		return 1;
	}
}
