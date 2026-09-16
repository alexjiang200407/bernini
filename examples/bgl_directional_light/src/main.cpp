#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <FlyCamera.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_messagebox.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
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
#include <cmath>
#include <core/glm.h>
#include <cstdint>
#include <exception>
#include <format>
#include <gamelib/AssetManager.h>
#include <headless/headless_render.h>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

// The sun, shown doing the three things a test cannot show: sweeping a terminator across geometry as
// it moves, adding to an environment that already has a sun of its own, and casting nothing.
//
// The row runs dielectric to metal left to right, which is what makes the sun's current limit
// visible: toggle it with L and the left of the row swings while the right does not move at all. A
// metal's diffuse is zero, so until the specular lobe lands a metal is lit by the environment alone.

namespace
{
	constexpr uint32_t c_Spheres      = 5;
	constexpr float    c_SphereRadius = 1.1f;
	constexpr float    c_Spacing      = 3.0f;
	constexpr float    c_GroundSize   = 60.0f;
	constexpr float    c_OrbitSeconds = 24.0f;

	struct Sun
	{
		float     azimuth   = glm::radians(35.0f);
		float     elevation = glm::radians(38.0f);
		glm::vec3 color{ 1.0f, 0.96f, 0.88f };
		float     intensity = 3.0f;
		bool      on        = true;
		bool      orbiting  = true;

		[[nodiscard]] bgl::DirectionalLightDesc
		Desc() const noexcept
		{
			return { .direction = headless::SunDirection(azimuth, elevation),
				     .color     = color,
				     .intensity = on ? intensity : 0.0f };
		}
	};

	void
	Report(const Sun& sun)
	{
		std::cout << std::format(
			"sun {}  intensity {:.2f}  azimuth {:.0f} deg  elevation {:.0f} deg  orbit {}\n",
			sun.on ? "on" : "off",
			sun.intensity,
			glm::degrees(sun.azimuth),
			glm::degrees(sun.elevation),
			sun.orbiting ? "running" : "paused");
	}
}

int
main(int argc, char** argv)
{
	auto headless = false;

	try
	{
		uint32_t    width    = 1280;
		uint32_t    height   = 720;
		uint32_t    frames   = 90;
		float       expScale = 0.35f;
		std::string shot     = "bgl_directional_light.png";
		auto        sun      = Sun();

		{
			CLI::App app{ "Bernini bgl_directional_light example" };
			app.set_help_flag("--help", "Print this help message and exit");
			app.add_option("-w,--width", width, "Width in pixels")->check(CLI::PositiveNumber);
			app.add_option("-h,--height", height, "Height in pixels")->check(CLI::PositiveNumber);
			app.add_option("-i,--intensity", sun.intensity, "Sun intensity")
				->check(CLI::NonNegativeNumber);
			app.add_option("--azimuth", sun.azimuth, "Sun azimuth in radians");
			app.add_option("--elevation", sun.elevation, "Sun elevation in radians");
			app.add_flag("--no-sun", "Start with the sun switched off");
			app.add_flag("--headless", headless, "Render offscreen for --frames and exit");
			app.add_option("--frames", frames, "Frames to render in --headless mode")
				->check(CLI::PositiveNumber);
			app.add_option("--out", shot, "Where --headless writes its PNG");
			app.add_option("--exposure-scale", expScale, "Trim on the environment's own exposure")
				->check(CLI::PositiveNumber);

			CLI11_PARSE(app, argc, argv);

			sun.on = app.count("--no-sun") == 0u;
		}

		auto wnd = std::optional<demo::DemoWindow>();
		if (!headless)
		{
			auto opts         = demo::WindowOptions{};
			opts.width        = static_cast<int>(width);
			opts.height       = static_cast<int>(height);
			opts.title        = "Bernini bgl_directional_light";
			opts.captureMouse = true;
			wnd.emplace(opts);
		}

		auto gfxOpts             = bgl::GraphicsOptions{};
		gfxOpts.enableDebugLayer = true;

		auto graphics = bgl::CreateGraphics(gfxOpts);

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = static_cast<int>(width);
		targetDesc.height   = static_cast<int>(height);
		targetDesc.headless = headless;
		targetDesc.wnd      = headless ? nullptr : wnd->NativeHandle();
		auto target         = graphics->CreateRenderTarget(targetDesc);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialIndices              = 200000;
		sceneDesc.initialVertexBufferByteSize = 2000000;
		sceneDesc.initialGeom                 = 32;
		sceneDesc.initialMeshlets             = 8192;
		sceneDesc.initialSubmeshes            = 32;
		sceneDesc.initialPbrMaterials         = 32;

		auto scene = graphics->CreateScene(std::move(sceneDesc));
		auto view  = graphics->CreateSceneView(scene, 64);

		auto assets = game::AssetManager(scene, "assets/Data");

		const auto env = assets.AcquireEnvironment("Authored/Environments/forest.benv");
		if (env.HasLighting())
		{
			view->SetEnvironmentMap({ env.irradiance, env.prefilter });
			// Trimmed below what the environment normalized itself to. At its own exposure this
			// scene sits on the flat top of the tone map, where the sun and the ambient compress
			// into the same near-white and the shaping this example exists to show is lost.
			view->SetExposure(env.exposure * expScale);
		}
		if (env.HasSky())
		{
			view->SetSkyBox({ env.skybox, env.skyMipLevel, 1.0f, env.skyRotationY });
		}

		view->SetDirectionalLight(sun.Desc());

		// The plane is authored in XY facing +Z, so it is laid flat by turning it onto its back.
		const auto ground = scene->AddPlaneGeom(
			1,
			1,
			c_GroundSize,
			c_GroundSize,
			scene->CreatePbrMaterial(
				{ .baseColorFactor = glm::vec4(0.20f, 0.19f, 0.18f, 1.0f),
		          .metallicFactor  = 0.0f,
		          .roughnessFactor = 0.95f }));

		view->CreateStaticMeshInstance(
			ground,
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));

		// Dielectric to metal, left to right. The sun reaches the left of this row and not yet the
		// right: a metal's kD is zero, so its whole answer is the specular lobe task 2 adds.
		for (uint32_t i = 0; i < c_Spheres; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(c_Spheres - 1u);

			const auto material = scene->CreatePbrMaterial(
				{ .baseColorFactor = glm::vec4(0.55f, 0.50f, 0.45f, 1.0f),
			      .metallicFactor  = t,
			      .roughnessFactor = 0.35f });

			const auto sphere = scene->AddSphereGeom(48, 32, c_SphereRadius, material);

			const float x =
				(static_cast<float>(i) - 0.5f * static_cast<float>(c_Spheres - 1u)) * c_Spacing;

			view->CreateStaticMeshInstance(
				sphere,
				glm::translate(glm::mat4(1.0f), glm::vec3(x, c_SphereRadius, 0.0f)));
		}

		const float aspect = static_cast<float>(width) / static_cast<float>(height);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 4.5f, 15.0f),
				glm::vec3(0.0f, 1.0f, 0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(55.0f), aspect, 0.5f, 500.0f);

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
				const float seconds = static_cast<float>(frame) * c_Step;

				if (sun.orbiting)
				{
					sun.azimuth =
						glm::radians(35.0f) + glm::two_pi<float>() * seconds / c_OrbitSeconds;
					view->SetDirectionalLight(sun.Desc());
				}

				job.time = seconds;
				graphics->DrawFrame(target, job);
			}

			graphics->ScreenshotPng(target, shot);
			std::cout << "wrote " << shot << '\n';
			return 0;
		}

		std::cout
			<< "WASD to fly, hold Shift and move the mouse to look.\n"
			   "  L      sun on/off -- the dielectric end of the row swings, the metal end does "
			   "not\n"
			   "  Space  pause the orbit\n"
			   "  [ ]    intensity down/up\n"
			   "  R      reset\n"
			   "Nothing casts a shadow: the row is lit through the ground, not onto it.\n\n";
		Report(sun);

		auto  clock   = demo::DeltaClock{};
		float elapsed = 0.0f;

		while (!wnd->ShouldClose())
		{
			auto changed = false;

			demo::PumpEvents([&sun, &changed](const SDL_Event& ev) {
				if (ev.type != SDL_EVENT_KEY_DOWN || ev.key.repeat != 0)
					return;

				switch (ev.key.key)
				{
				case SDLK_L:
					sun.on = !sun.on;
					break;
				case SDLK_SPACE:
					sun.orbiting = !sun.orbiting;
					break;
				case SDLK_LEFTBRACKET:
					sun.intensity = std::max(0.0f, sun.intensity - 0.1f);
					break;
				case SDLK_RIGHTBRACKET:
					sun.intensity += 0.1f;
					break;
				case SDLK_R:
					sun = Sun();
					break;
				default:
					return;
				}

				changed = true;
			});

			const float dt = clock.Tick();
			elapsed += dt;

			if (demo::ApplyFlyCam(camera, dt))
			{
				job.camera = camera;
			}

			if (sun.orbiting)
			{
				sun.azimuth += glm::two_pi<float>() * dt / c_OrbitSeconds;
			}

			if (changed)
			{
				Report(sun);
			}

			view->SetDirectionalLight(sun.Desc());

			job.time = elapsed;
			graphics->DrawFrame(target, job);
		}

		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "bgl_directional_light: " << e.what() << '\n';

		if (!headless)
		{
			SDL_ShowSimpleMessageBox(
				SDL_MESSAGEBOX_ERROR,
				"bgl_directional_light",
				e.what(),
				nullptr);
		}

		return 1;
	}
}
