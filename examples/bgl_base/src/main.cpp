#include <assetlib/image_io.h>
#include <bgl/bgl.h>
#include <core/platform/Platform.h>
#include <format>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

struct EventVisitor : public core::IPlatformEventVisitor
{
	// Clear per-frame accumulated input after it has been applied to the camera.
	void
	Reset()
	{
		changedPosition = false;
		changedRotation = false;
		forwardDelta    = 0.0f;
		rightDelta      = 0.0f;
		yawDelta        = 0.0f;
		pitchDelta      = 0.0f;
	}

	void
	Visit(const core::KeyEvent& e, float dt) override
	{
		// VK_SHIFT gates mouse-look so the cursor stays usable otherwise.
		if (e.GetKeyCode() == 16)  // VK_SHIFT
		{
			shiftDown = !e.IsReleased();
			return;
		}

		if (!e.IsHeld())
		{
			return;
		}

		const float moveSpeed = dt * c_MoveUnitsPerSecond;
		switch (e.GetKeyCode())
		{
		case 87:  // W
			forwardDelta += moveSpeed;
			changedPosition = true;
			break;
		case 83:  // S
			forwardDelta -= moveSpeed;
			changedPosition = true;
			break;
		case 65:  // A
			rightDelta -= moveSpeed;
			changedPosition = true;
			break;
		case 68:  // D
			rightDelta += moveSpeed;
			changedPosition = true;
			break;
		default:
			break;
		}
	}

	void
	Visit(const core::MouseEvent& e, float dt) override
	{
		(void)dt;
		using Action = core::MouseEvent::Action;

		if (e.GetActions().all(Action::kMove) && shiftDown)
		{
			const auto& delta = e.GetDelta();
			yawDelta -= static_cast<float>(delta.dx) * c_MouseRadiansPerPixel;
			pitchDelta -= static_cast<float>(delta.dy) * c_MouseRadiansPerPixel;
			changedRotation = true;
		}
	}

	static constexpr float c_MoveUnitsPerSecond   = 15.0f;
	static constexpr float c_MouseRadiansPerPixel = 0.005f;

	bool  changedPosition = false;
	bool  changedRotation = false;
	float forwardDelta    = 0.0f;
	float rightDelta      = 0.0f;
	float yawDelta        = 0.0f;
	float pitchDelta      = 0.0f;
	bool  shiftDown       = false;
};

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	try
	{
		auto opts = core::PlatformOptions{};

		opts.width     = 800;
		opts.height    = 600;
		opts.resizable = false;
		opts.decorated = false;
		opts.mode      = core::PlatformOptions::Mode::BorderlessWindowed;

		auto wnd = core::IPlatform::Create(opts);

		auto gfxOpts                     = bgl::GraphicsOptions{};
		gfxOpts.enableDebugLayer         = true;
		gfxOpts.enableGPUValidationLayer = false;
		gfxOpts.enablePixDebug           = true;
		gfxOpts.logLevel                 = bgl::GraphicsOptions::LogLevel::kTrace;

		auto graphics = bgl::CreateGraphics(gfxOpts);

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = opts.width;
		targetDesc.height   = opts.height;
		targetDesc.headless = false;
		targetDesc.wnd      = wnd->GetNativeHandle();
		auto target         = graphics->CreateRenderTarget(targetDesc);

		auto visitor = EventVisitor();

		auto sceneDesc                    = bgl::SceneDesc();
		sceneDesc.maxIndices              = 10000;
		sceneDesc.maxVertexBufferByteSize = 100000;
		sceneDesc.maxGeom                 = 100;
		sceneDesc.maxMeshlets             = 1000;
		sceneDesc.maxSubmeshes            = 100;
		sceneDesc.maxPbrMaterials         = 100;

		auto scene = graphics->CreateScene(std::move(sceneDesc));
		auto view  = graphics->CreateSceneView(scene, 100);

		scene->SetEnvironmentMap(
			{ scene->AddTextureAsset(assetlib::loadDDS("assets/iem.dds")),
		      scene->AddTextureAsset(assetlib::loadDDS("assets/pmrem.dds")),
		      scene->AddTextureAsset(assetlib::loadDDS("assets/brdf_lut.dds")) });

		auto metalMat = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(1.0f), .metallicFactor = .6f, .roughnessFactor = .3f });

		auto cube   = scene->AddCubeGeom(metalMat);
		auto sphere = scene->AddSphereGeom(32, 32, 5.0f, metalMat);

		auto transform = glm::mat4(1.0f);

		auto inst2 = view->CreateStaticMeshInstance(sphere, transform);

		const float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto context   = bgl::RenderContext{};
		context.view   = view;
		context.camera = camera;
		context.viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		bool firstFrame = true;
		for (auto res = wnd->Process(&visitor); res != core::IPlatform::kClose;
		     res      = wnd->Process(&visitor))
		{
			if (res == core::IPlatform::kProcess)
			{
				// WASD to fly, hold Shift + move the mouse to look around.
				if (visitor.changedRotation)
				{
					camera.RotateYawPitch(visitor.yawDelta, visitor.pitchDelta);
				}
				if (visitor.changedPosition)
				{
					camera.MoveAlongView(visitor.forwardDelta);
					camera.MoveAlongRight(visitor.rightDelta);
				}
				if (visitor.changedPosition || visitor.changedRotation)
				{
					// context holds a copy of the camera; refresh it after moving.
					context.camera = camera;
				}
				visitor.Reset();
			}

			graphics->DrawFrame(target, context);

			if (firstFrame)
			{
				graphics->ScreenshotRaw(target, "bgl_base.dds");
			}

			firstFrame = false;
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
