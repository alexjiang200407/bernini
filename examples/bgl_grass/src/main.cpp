#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <FlyCamera.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_messagebox.h>
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/grass_patch.h>
#include <assetlib_structs/BGrass.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialHandle.h>
#include <bgl/RenderJob.h>
#include <bgl/SkyboxDesc.h>  // IWYU pragma: keep
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/WindDesc.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <gamelib/AssetManager.h>
#include <headless/headless_render.h>
#include <iostream>
#include <optional>
#include <string>

// A grass look grown on a patch of bare ground, in a window: walk into it and blow on it. What the
// headless viewer cannot show is the motion, and the wind here is changed while it runs.

namespace
{
	constexpr float c_EyeHeight = 1.6f;
	constexpr float c_WindStep  = 0.05f;
	constexpr float c_TurnStep  = 0.2617994f;  // 15 degrees

	struct Wind
	{
		float heading  = glm::radians(20.0f);  // about the up axis, from +X toward +Z
		float strength = 0.3f;
		bool  gusts    = true;

		[[nodiscard]] bgl::WindDesc
		Desc() const noexcept
		{
			return { .direction    = glm::vec3(std::cos(heading), 0.0f, std::sin(heading)),
				     .strength     = strength,
				     .gustStrength = gusts ? strength : 0.0f };
		}
	};

	void
	Report(const Wind& wind)
	{
		std::cout << std::format(
			"wind {:.2f}  heading {:.0f} deg  gusts {}\n",
			wind.strength,
			glm::degrees(wind.heading),
			wind.gusts ? "on" : "off");
	}
}

int
main(int argc, char** argv)
{
	auto headless = false;

	try
	{
		uint32_t    width   = 1280;
		uint32_t    height  = 720;
		uint32_t    frames  = 90;
		std::string project = "assets/Data";
		std::string grass   = "Authored/Grass/meadow.bgrass";
		std::string shot    = "bgl_grass.png";
		float       spacing = 0.25f;
		float       sun     = 2.0f;
		auto        wind    = Wind();

		{
			CLI::App app{ "A grass look on a patch of ground, in wind you can change" };
			app.set_help_flag("--help", "Print this help message and exit");
			app.add_option("-w,--width", width, "Width in pixels")->check(CLI::PositiveNumber);
			app.add_option("-h,--height", height, "Height in pixels")->check(CLI::PositiveNumber);
			app.add_option("--project", project, "The project's Data directory");
			app.add_option("--grass", grass, "The .bgrass to grow, keyed against --project");
			app.add_option("--spacing", spacing, "Metres between the patch's clumps")
				->check(CLI::PositiveNumber);
			app.add_option("--wind", wind.strength, "Starting wind strength, in [0, 1]")
				->check(CLI::Range(0.0f, 1.0f));
			app.add_option("--sun", sun, "Sun intensity; 0 leaves only the environment")
				->check(CLI::NonNegativeNumber);
			app.add_flag("--headless", headless, "Render offscreen for --frames and exit");
			app.add_option("--frames", frames, "Frames to render in --headless mode")
				->check(CLI::PositiveNumber);
			app.add_option("--out", shot, "Where --headless writes its PNG");

			CLI11_PARSE(app, argc, argv);
		}

		const auto store = assetlib::AssetStore(std::filesystem::path(project));
		if (!grass.ends_with(".bgrass") || !store.Exists(grass))
		{
			core::throw_runtime_error(
				"--grass {} names no .bgrass in {}",
				grass,
				std::filesystem::absolute(project).string());
		}
		const auto look = store.Load<assetlib::BGrass>(grass);

		auto wnd = std::optional<demo::DemoWindow>();
		if (!headless)
		{
			auto opts         = demo::WindowOptions{};
			opts.width        = static_cast<int>(width);
			opts.height       = static_cast<int>(height);
			opts.title        = "Bernini bgl_grass";
			opts.captureMouse = true;
			wnd.emplace(opts);
		}

		auto gfxOpts             = bgl::GraphicsOptions{};
		gfxOpts.enableDebugLayer = true;

		// The look's material may shade through a surface the project authors, and surfaces are
		// registered only at creation.
		const auto surfaceDir = std::filesystem::path(project) / "Authored" / "Shaders";
		if (std::filesystem::is_directory(surfaceDir))
			gfxOpts.surfaceShaderDir = surfaceDir;

		auto graphics = bgl::CreateGraphics(gfxOpts);

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = static_cast<int>(width);
		targetDesc.height   = static_cast<int>(height);
		targetDesc.headless = headless;
		targetDesc.wnd      = headless ? nullptr : wnd->NativeHandle();
		auto target         = graphics->CreateRenderTarget(targetDesc);

		auto scene  = graphics->CreateScene(bgl::SceneDesc());
		auto view   = graphics->CreateSceneView(scene, 64);
		auto assets = game::AssetManager(scene, project);

		// The environment ships with the engine's own fixture tree, which is not necessarily the
		// project the look comes from.
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
		view->SetWind(wind.Desc());

		// Twice the fade end across, so the field thins to nothing before its edge.
		auto patch    = assetlib::GrassPatchDesc();
		patch.size    = 2.0f * look.density.fadeEnd;
		patch.spacing = spacing;

		const bgl::MaterialHandle ground = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(0.22f, 0.19f, 0.15f, 1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 1.0f });

		// The patch is authored in XY facing +Z, as a plane is, so it is laid flat on its back.
		assets.CreateInstance(
			view,
			assets.CreateGrassPatch(patch, grass, ground),
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));

		const float aspect = static_cast<float>(width) / static_cast<float>(height);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, c_EyeHeight, 6.0f),
				glm::vec3(0.0f, 0.3f, 0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.05f, 2.0f * patch.size);

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
			"{}: a {:.0f} m patch, a clump every {} m.\n"
			"WASD to fly, hold Shift and move the mouse to look.\n"
			"  [ ]    wind down/up\n"
			"  , .    turn the wind left/right\n"
			"  G      gusts on/off\n"
			"  R      reset the wind\n\n",
			grass,
			patch.size,
			patch.spacing);
		Report(wind);

		auto  clock   = demo::DeltaClock{};
		float elapsed = 0.0f;

		while (!wnd->ShouldClose())
		{
			auto changed = false;

			demo::PumpEvents([&wind, &changed](const SDL_Event& ev) {
				if (ev.type != SDL_EVENT_KEY_DOWN || ev.key.repeat != 0)
					return;

				switch (ev.key.key)
				{
				case SDLK_LEFTBRACKET:
					wind.strength = std::max(0.0f, wind.strength - c_WindStep);
					break;
				case SDLK_RIGHTBRACKET:
					wind.strength = std::min(1.0f, wind.strength + c_WindStep);
					break;
				case SDLK_COMMA:
					wind.heading -= c_TurnStep;
					break;
				case SDLK_PERIOD:
					wind.heading += c_TurnStep;
					break;
				case SDLK_G:
					wind.gusts = !wind.gusts;
					break;
				case SDLK_R:
					wind = Wind();
					break;
				default:
					return;
				}

				changed = true;
			});

			const float dt = clock.Tick();
			elapsed += dt;

			if (demo::ApplyFlyCam(camera, dt))
				job.camera = camera;

			if (changed)
			{
				view->SetWind(wind.Desc());
				Report(wind);
			}

			job.time = elapsed;
			graphics->DrawFrame(target, job);
		}

		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "bgl_grass: " << e.what() << '\n';

		if (!headless)
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "bgl_grass", e.what(), nullptr);

		return 1;
	}
}
