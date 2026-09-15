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
#include <bgl/glm.h>
#include <bgl/types/BlobShadowDesc.h>
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
	constexpr float c_CasterRadius = 0.35f;
	constexpr float c_Hover        = 1.75f;
	constexpr float c_Bob          = 0.2f;
	constexpr float c_DiscRadius   = 0.9f;
	constexpr float c_Intensity    = 0.85f;

	glm::mat4
	Box(const glm::vec3 centre, const glm::vec3 halfExtents)
	{
		return glm::scale(glm::translate(glm::mat4(1.0f), centre), halfExtents);
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
		float       travel       = 4.0f;
		float       period       = 8.0f;
		float       fadeHeight   = 2.5f;
		bool        taaEnabled   = true;
		uint32_t    frames       = 120;
		std::string dataRootPath = "assets/Data";

		{
			auto app = CLI::App{ "A hovering caster whose blob shadow drapes over static crates" };
			app.add_option("--width", width, "Window width")->check(CLI::PositiveNumber);
			app.add_option("--height", height, "Window height")->check(CLI::PositiveNumber);
			app.add_option("--travel", travel, "How far the caster slides, in world units");
			app.add_option("--period", period, "Seconds for one round trip");
			app.add_option("--fade-height", fadeHeight, "Gap at which the shadow has fully faded")
				->check(CLI::PositiveNumber);
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
			                         .title  = "bernini - blob shadow decal" });
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
		sceneDesc.initialGeom                 = 16;
		sceneDesc.initialSubmeshes            = 64;
		sceneDesc.initialMeshlets             = 1024;
		sceneDesc.initialVertexBufferByteSize = 262144;
		sceneDesc.initialIndices              = 16384;
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

		// Matte and mid-grey, so the shadow reads as darkening rather than as a material change.
		const auto groundMaterial = scene->CreatePbrMaterial(
			bgl::PbrMaterialDesc{ .baseColorFactor = glm::vec4(0.45f, 0.45f, 0.45f, 1.0f),
		                          .metallicFactor  = 0.0f,
		                          .roughnessFactor = 0.9f });
		const auto crateMaterial = scene->CreatePbrMaterial(
			bgl::PbrMaterialDesc{ .baseColorFactor = glm::vec4(0.55f, 0.38f, 0.22f, 1.0f),
		                          .metallicFactor  = 0.0f,
		                          .roughnessFactor = 0.8f });
		const auto casterMaterial = scene->CreatePbrMaterial(
			bgl::PbrMaterialDesc{ .baseColorFactor = glm::vec4(0.85f, 0.85f, 0.88f, 1.0f),
		                          .metallicFactor  = 0.0f,
		                          .roughnessFactor = 0.45f });

		const bgl::GeomHandle ground = scene->AddPlaneGeom(1, 1, 30.0f, 30.0f, groundMaterial);
		const bgl::GeomHandle crate  = scene->AddCubeGeom(crateMaterial);
		const bgl::GeomHandle ball   = scene->AddSphereGeom(32, 24, c_CasterRadius, casterMaterial);

		// Plane geoms are authored in XY; this lays the ground flat with its normal up.
		const glm::mat4 flat =
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		(void)view->CreateStaticMeshInstance(ground, flat);

		// Two receivers of different heights across the caster's path: the disc should climb onto
		// each top and drape over its edges rather than vanish beneath it. The cube geom spans
		// [-1, 1] on every axis, so the half extents are the box's half size.
		(void)view->CreateStaticMeshInstance(
			crate,
			Box(glm::vec3(2.5f, 0.6f, 0.0f), glm::vec3(1.5f, 0.6f, 1.5f)));
		(void)view->CreateStaticMeshInstance(
			crate,
			Box(glm::vec3(-2.5f, 0.3f, 0.0f), glm::vec3(1.0f, 0.3f, 1.0f)));

		const auto shadow = bgl::BlobShadowDesc{ .radius     = c_DiscRadius,
			                                     .intensity  = c_Intensity,
			                                     .fadeHeight = fadeHeight };

		const bgl::MeshInstanceHandle hoverer = view->CreateStaticMeshInstance(
			ball,
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, c_Hover, 0.0f)));
		view->SetBlobShadow(hoverer, shadow);

		// A grounded twin for contrast: its disc is at full strength and never moves.
		const bgl::MeshInstanceHandle rester = view->CreateStaticMeshInstance(
			ball,
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, c_CasterRadius, 2.5f)));
		view->SetBlobShadow(rester, shadow);

		const float aspect = static_cast<float>(width) / static_cast<float>(height);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 5.0f, 11.0f),
				glm::vec3(0.0f, 0.8f, 0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto job     = bgl::RenderJob{};
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(width), static_cast<float>(height));

		const auto slide = [&](float seconds) {
			const float phase =
				period > 0.0f ? seconds * 2.0f * static_cast<float>(core::c_Pi) / period : 0.0f;
			view->SetInstanceTransform(
				hoverer,
				glm::translate(
					glm::mat4(1.0f),
					glm::vec3(
						travel * std::sin(phase),
						c_Hover + c_Bob * std::sin(2.0f * phase),
						0.0f)));
		};

		if (headless)
		{
			// A fixed step, so the run is the same every time. The default frame count parks the
			// caster on the tall crate's edge, the disc half on its top and half on the ground.
			constexpr float c_Step = 1.0f / 60.0f;

			for (uint32_t frame = 0; frame < frames; ++frame)
			{
				const float seconds = static_cast<float>(frame) * c_Step;
				slide(seconds);
				job.time = seconds;
				graphics->DrawFrame(target, job);
			}

			graphics->ScreenshotPng(target, "bgl_blob_shadow.png");
			return 0;
		}

		std::cout
			<< "WASD to fly, hold Shift and move the mouse to look. The hovering ball slides over "
			   "two crates: its shadow climbs onto each top, drapes over the edges, and fades "
			   "with the gap. The resting ball's disc stays grounded.\n";

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
		std::cerr << "bgl_blob_shadow: " << e.what() << '\n';

		if (!headless)
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "bgl_blob_shadow", e.what(), nullptr);
		}

		return 1;
	}
}
