#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <FlyCamera.h>
#include <RmlInput.h>
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <bgl/bgl.h>
#include <gamelib/AssetManager.h>
#include <gamelib/ui/UiRenderer.h>
#include <gamelib/ui/UiRuntime.h>

/**
 * The three layers a menu is made of: a styled document underneath, a live 3D render inside it,
 * and controls over the top.
 *
 * The middle layer is the one worth reading. It is not an image and not a pass -- it is the sphere
 * scene drawn every tick into a headless render target of its own, which the document then shows
 * through `<img src="target://preview"/>`. The window's own frame draws no scene at all: it is a
 * UI-only frame with the preview composited into it as a texture.
 */
int
main(int argc, char** argv)
{
	try
	{
		uint32_t width  = 1024;
		uint32_t height = 640;

		// Draws a few frames, writes the window's frame to a PNG and exits: what makes the layer
		// stack checkable without a person at the keyboard.
		std::string screenshot;
		uint32_t    screenshotFrame = 8;

		{
			CLI::App app{ "Bernini bgl_ui example" };
			app.set_help_flag("--help", "Print this help message and exit");
			app.add_option("-w,--width", width, "Window width in pixels")
				->check(CLI::PositiveNumber);
			app.add_option("-h,--height", height, "Window height in pixels")
				->check(CLI::PositiveNumber);

			app.add_option("--screenshot", screenshot, "Write a PNG of the frame and exit");
			app.add_option("--screenshot-frame", screenshotFrame, "Which frame to capture")
				->check(CLI::PositiveNumber);

			CLI11_PARSE(app, argc, argv);
		}

		auto opts       = demo::WindowOptions{};
		opts.width      = static_cast<int>(width);
		opts.height     = static_cast<int>(height);
		opts.title      = "Bernini bgl_ui";
		opts.borderless = true;

		auto wnd = demo::DemoWindow{ opts };

		auto gfxOpts             = bgl::GraphicsOptions{};
		gfxOpts.enableDebugLayer = true;

		auto graphics = bgl::CreateGraphics(gfxOpts);

		// The window's frame draws only the UI, so it accumulates nothing: TAA off, and a frame
		// with no Draw is legal.
		auto targetDesc       = bgl::RenderTargetDesc{};
		targetDesc.width      = static_cast<int>(width);
		targetDesc.height     = static_cast<int>(height);
		targetDesc.headless   = false;
		targetDesc.taaEnabled = false;
		targetDesc.wnd        = wnd.NativeHandle();

		auto target = graphics->CreateRenderTarget(targetDesc);

		// The preview: its own target, its own size, drawn every tick.
		constexpr uint32_t c_PreviewSize = 512;

		auto previewDesc     = bgl::RenderTargetDesc{};
		previewDesc.width    = static_cast<int>(c_PreviewSize);
		previewDesc.height   = static_cast<int>(c_PreviewSize);
		previewDesc.headless = true;

		auto preview = graphics->CreateRenderTarget(previewDesc);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialIndices              = 10000;
		sceneDesc.initialVertexBufferByteSize = 100000;
		sceneDesc.initialGeom                 = 100;
		sceneDesc.initialMeshlets             = 1000;
		sceneDesc.initialSubmeshes            = 100;
		sceneDesc.initialPbrMaterials         = 100;

		auto scene = graphics->CreateScene(std::move(sceneDesc));
		auto view  = graphics->CreateSceneView(scene, 100);

		auto assets = game::AssetManager(scene, "assets/Data");

		const auto env = assets.AcquireEnvironment("Authored/Environments/forest.benv");
		if (env.HasLighting())
			view->SetEnvironmentMap({ env.irradiance, env.prefilter });
		view->SetExposure(env.exposure);

		if (env.HasSky())
			view->SetSkyBox({ env.skybox, env.skyMipLevel, 1.0f, env.skyRotationY });

		auto material = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(0.9f, 0.75f, 0.35f, 1.0f),
		      .metallicFactor  = 0.9f,
		      .roughnessFactor = 0.25f });

		auto sphere = scene->AddSphereGeom(48, 48, 2.0f, material);
		view->CreateStaticMeshInstance(sphere, glm::mat4(1.0f));

		auto camera = bgl::Camera();
		camera.LookAt({ 0.0f, 0.0f, 8.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f })
			.Perspective(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);

		auto previewJob   = bgl::RenderJob{};
		previewJob.view   = view;
		previewJob.camera = camera;
		previewJob.viewport =
			bgl::Viewport(static_cast<float>(c_PreviewSize), static_cast<float>(c_PreviewSize));

		game::UiRenderer uiRenderer(*graphics, assets.GetStore());
		uiRenderer.RegisterTarget("preview", preview);

		game::UiRuntime uiRuntime(assets.GetStore(), uiRenderer.Interface());
		uiRuntime.LoadFontFace("Authored/Fonts/Lato-Regular.ttf");

		game::UiContextPtr uiContext = uiRuntime.CreateContext("menu", width, height);

		// What the document binds against. The names are the document's; nothing here parses it.
		int   plays    = 0;
		float frameMs  = 0.0f;
		bool  spinning = true;

		Rml::DataModelConstructor model = uiContext->Get().CreateDataModel("menu");
		if (!model)
			throw std::runtime_error("the UI data model could not be created");

		model.Bind("plays", &plays);
		model.Bind("frameMs", &frameMs);
		model.Bind("spinning", &spinning);

		Rml::DataModelHandle modelHandle = model.GetModelHandle();

		Rml::ElementDocument* document = uiContext->LoadDocument("Authored/UI/demo.rml");
		document->Show();

		auto     clock = demo::DeltaClock{};
		float    spin  = 0.0f;
		uint32_t frame = 0;

		while (!wnd.ShouldClose())
		{
			// One pump, two consumers: every event reaches the UI first (plan ADR-10).
			demo::PumpEvents([&](const SDL_Event& event) {
				static_cast<void>(demo::ForwardToUi(uiContext->Get(), event));
			});

			// What gates the fly-cam is the element under the cursor, not ProcessMouse*'s consumed
			// bool: that bool is false whenever the cursor is over an interactive element, the
			// panel among them, so the two conditions can never both hold. The bool answers
			// whether a click landed on nothing, which is a different question.
			const Rml::Element* hovered   = uiContext->Get().GetHoverElement();
			const bool          overPanel = hovered != nullptr && hovered->GetId() == "preview";

			const float dt = clock.Tick();
			uiRuntime.AdvanceTime(static_cast<double>(dt));

			frameMs = dt * 1000.0f;
			modelHandle.DirtyVariable("frameMs");

			// Drained every frame, into a copy. ApplyFlyCam is the only consumer of SDL's
			// accumulated relative motion, so the call cannot be skipped -- and it moves what it
			// is given, so the copy is what keeps an off-panel gesture from banking into the
			// camera and arriving as one jump when the panel is next entered.
			auto       flyCam = camera;
			const bool flew   = demo::ApplyFlyCam(flyCam, dt);

			// The preview's own frame: a real scene, drawn every tick into its own target. An
			// instance's transform is fixed at creation, so the camera is what moves.
			//
			// Flying takes the camera; hovering does not. The orbit recomputes the camera from
			// `spin` every frame, so the two cannot share it -- but the one that yields is the
			// orbit, and only once the cursor is over the preview *and* the user actually moved
			// something. SPIN is then what hands it back, which is the whole reason that button
			// exists.
			if (overPanel && flew)
			{
				camera            = flyCam;
				previewJob.camera = camera;

				if (spinning)
				{
					spinning = false;
					modelHandle.DirtyVariable("spinning");
				}
			}
			else if (spinning)
			{
				spin += dt * 0.6f;

				camera
					.LookAt(
						{ std::sin(spin) * 8.0f, 1.5f, std::cos(spin) * 8.0f },
						{ 0.0f, 0.0f, 0.0f },
						{ 0.0f, 1.0f, 0.0f })
					.Perspective(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);

				previewJob.camera = camera;
			}

			graphics->DrawFrame(preview, previewJob);

			// The window's frame: the UI alone, with the preview composited into it.
			uiContext->Get().Update();

			graphics->BeginFrame(target);
			uiRenderer.Render(*graphics, *uiContext);
			graphics->EndFrame();

			if (!screenshot.empty() && ++frame >= screenshotFrame)
			{
				graphics->ScreenshotPng(target, screenshot);
				break;
			}
		}
	}
	catch (const std::runtime_error& e)
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Bernini - Fatal Error", e.what(), nullptr);
	}

	return 0;
}
