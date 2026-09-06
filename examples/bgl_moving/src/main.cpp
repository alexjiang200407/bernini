#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <FlyCamera.h>
#include <SDL3/SDL_messagebox.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/RenderJob.h>
#include <bgl/SkyboxDesc.h>  // IWYU pragma: keep
#include <bgl/Viewport.h>
#include <bgl/bgl.h>
#include <bgl/glm.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <cmath>
#include <core/math.h>
#include <cstdint>
#include <exception>
#include <gamelib/AssetManager.h>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace
{
	constexpr float c_Separation = 2.5f;
	constexpr float c_CubeZ      = 0.0f;
	constexpr float c_CameraZ    = 9.0f;

	glm::mat4
	At(float x)
	{
		return glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, c_CubeZ));
	}
}

int
main(int argc, char** argv)
{
	// Outside the try so the handler knows whether anyone is looking at a window rather than at
	// stderr.
	bool headless = false;

	try
	{
		uint32_t    width        = 1280;
		uint32_t    height       = 720;
		float       travel       = 3.0f;
		float       period       = 4.0f;
		bool        taaEnabled   = true;
		uint32_t    frames       = 90;
		std::string dataRootPath = "assets/Data";

		{
			auto app = CLI::App{ "A moving instance beside a still one, for judging its motion" };
			app.add_option("--width", width, "Window width")->check(CLI::PositiveNumber);
			app.add_option("--height", height, "Window height")->check(CLI::PositiveNumber);
			app.add_option("--travel", travel, "How far the left cube slides, in world units");
			app.add_option("--period", period, "Seconds for one round trip");
			app.add_flag("!--no-taa", taaEnabled, "Draw without temporal antialiasing");
			app.add_flag("--headless", headless, "Render offscreen for --frames and exit");
			app.add_option("--frames", frames, "Frames to render in --headless mode")
				->check(CLI::PositiveNumber);
			app.add_option("--data-root", dataRootPath, "Data root");
			CLI11_PARSE(app, argc, argv);
		}

		// Headless renders offscreen, so the example is runnable unattended -- which is how anything
		// but a person can tell it still starts.
		std::optional<demo::DemoWindow> wnd;
		if (!headless)
		{
			wnd.emplace(
				demo::WindowOptions{ .width  = static_cast<int>(width),
			                         .height = static_cast<int>(height),
			                         .title  = "bernini - moving instance" });
		}

		auto gfxOpts             = bgl::GraphicsOptions{};
		gfxOpts.enableDebugLayer = true;

		auto graphics = bgl::CreateGraphics(gfxOpts);

		auto targetDesc       = bgl::RenderTargetDesc{};
		targetDesc.width      = static_cast<int>(width);
		targetDesc.height     = static_cast<int>(height);
		targetDesc.headless   = headless;
		targetDesc.wnd        = headless ? nullptr : wnd->NativeHandle();
		targetDesc.taaEnabled = taaEnabled;

		auto target = graphics->CreateRenderTarget(targetDesc);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 8;
		sceneDesc.initialSubmeshes            = 32;
		sceneDesc.initialMeshlets             = 64;
		sceneDesc.initialVertexBufferByteSize = 65536;
		sceneDesc.initialIndices              = 4096;
		sceneDesc.initialPbrMaterials         = 8;

		auto scene = graphics->CreateScene(std::move(sceneDesc));
		auto view  = graphics->CreateSceneView(scene, 16);

		auto assets = game::AssetManager(scene, dataRootPath);

		const auto env = assets.AcquireEnvironment("Authored/Environments/forest.benv");
		if (env.HasLighting())
			view->SetEnvironmentMap({ env.irradiance, env.prefilter });
		view->SetExposure(env.exposure);

		if (env.HasSky())
		{
			view->SetSkyBox({ env.skybox, env.skyMipLevel, 1.0f, env.skyRotationY });
		}

		// A mid-roughness dielectric: bright enough to read against the sky, matte enough that the
		// silhouette is what draws the eye rather than a specular highlight sliding over it.
		const auto material = scene->CreatePbrMaterial(
			bgl::PbrMaterialDesc{ .baseColorFactor = glm::vec4(0.85f, 0.85f, 0.88f, 1.0f),
		                          .metallicFactor  = 0.0f,
		                          .roughnessFactor = 0.45f });

		const bgl::GeomHandle cube = scene->AddCubeGeom(material);

		// One geom, two placements: anything that differs between them is the placement, since the
		// geometry, the material and the lighting are shared.
		const bgl::MeshInstanceHandle mover =
			view->CreateStaticMeshInstance(cube, At(-c_Separation));
		(void)view->CreateStaticMeshInstance(cube, At(c_Separation));

		const float aspect = static_cast<float>(width) / static_cast<float>(height);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, c_CameraZ),
				glm::vec3(0.0f, 0.0f, c_CameraZ - 1.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto job     = bgl::RenderJob{};
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(width), static_cast<float>(height));

		// A sine rather than a sawtooth: it reverses without a jump, so every frame's velocity is one
		// a motion vector can describe and nothing here is accidentally testing the epoch instead.
		const auto slide = [&](float seconds) {
			const float phase =
				period > 0.0f ? seconds * 2.0f * static_cast<float>(core::c_Pi) / period : 0.0f;
			view->SetInstanceTransform(mover, At(-c_Separation + travel * std::sin(phase)));
		};

		if (headless)
		{
			// A fixed step, so the run is the same every time and the cube has actually moved by
			// the frame the screenshot is taken on.
			constexpr float c_Step = 1.0f / 60.0f;

			for (uint32_t frame = 0; frame < frames; ++frame)
			{
				const float seconds = static_cast<float>(frame) * c_Step;
				slide(seconds);
				job.time = seconds;
				graphics->DrawFrame(target, job);
			}

			graphics->ScreenshotPng(target, "bgl_moving.png");
			return 0;
		}

		std::cout
			<< "WASD to fly, hold Shift and move the mouse to look. The left cube slides; the "
			   "right one never moves.\n";

		auto  clock   = demo::DeltaClock{};
		float elapsed = 0.0f;

		while (!wnd->ShouldClose())
		{
			demo::PumpEvents();

			const float dt = clock.Tick();
			elapsed += dt;

			if (demo::ApplyFlyCam(camera, dt))
			{
				job.camera = camera;
			}

			slide(elapsed);

			job.time = elapsed;
			graphics->DrawFrame(target, job);
		}

		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "bgl_moving: " << e.what() << '\n';

		if (!headless)
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "bgl_moving", e.what(), nullptr);
		}

		return 1;
	}
}
