#include <concepts>
#include <core/type_traits.h>
#include <core/win/Window.h>
#include <gfx/GfxHandle.h>
#include <gfx/Vec3.h>
#include <gfx/ffi/gfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

struct BerniniGraphicseErrorChecker
{};

static const inline BerniniGraphicseErrorChecker berniniErrChecker;

namespace
{
	void
	operator>>(GfxResult berniniResult, BerniniGraphicseErrorChecker)
	{
		if (berniniResult != GFX_RESULT_OK)
		{
			auto errorInfo = getLastError();
			if (errorInfo.result == GFX_RESULT_OK)
			{
				errorInfo = { .result  = berniniResult,
					          .title   = "Unknown Error",
					          .message = "An unknown error has occurred." };
			}
			throw std::runtime_error{ std::format("{}: {}", errorInfo.title, errorInfo.message) };
		}
	}
}

struct EventVisitor : public core::win::IWindowEventVisitor
{
	void
	Reset()
	{
		changedPosition = false;
		changedRotation = false;
		rightDelta      = 0.0f;
		forwardDelta    = 0.0f;
		mouseDeltaX     = 0.0f;
		mouseDeltaY     = 0.0f;
	}

	void
	Visit(const core::win::KeyEvent& e, float dt) override
	{
		float moveSpeed = dt * 2.0f;

		if (e.GetKeyCode() == 16)  // Shift
		{
			shiftDown = e.IsHeld();
		}

		if (!e.IsHeld())
		{
			return;
		}

		switch (e.GetKeyCode())
		{
		case 87:  // W
			changedPosition = true;
			forwardDelta -= moveSpeed;
			break;
		case 65:  // A
			changedPosition = true;
			rightDelta -= moveSpeed;
			break;
		case 83:  // S
			changedPosition = true;
			forwardDelta += moveSpeed;
			break;
		case 68:  // D
			changedPosition = true;
			rightDelta += moveSpeed;
			break;

		default:
			break;
		}
	}

	void
	Visit(const core::win::MouseEvent& e, float dt) override
	{
		(void)dt;
		auto actions = e.GetActions();
		using Action = core::win::MouseEvent::Action;

		struct ActionInfo
		{
			Action      flag;
			const char* name;
		};

		constexpr ActionInfo allActions[] = {
			{ Action::kLPress, "Left Press" },       { Action::kLHeld, "Left Held" },
			{ Action::kLRelease, "Left Release" },   { Action::kRPress, "Right Press" },
			{ Action::kRHeld, "Right Held" },        { Action::kRRelease, "Right Release" },
			{ Action::kMPress, "Middle Press" },     { Action::kMHeld, "Middle Held" },
			{ Action::kMRelease, "Middle Release" },
		};

		if (actions.All(Action::kMove) && shiftDown)
		{
			const auto& delta = e.GetDelta();
			mouseDeltaX += (static_cast<float>(delta.dx) * 0.05f * dt);
			mouseDeltaY += (static_cast<float>(delta.dy) * 0.05f * dt);
			changedRotation = true;
		}
	}

	bool  changedPosition = false;
	bool  changedRotation = false;
	float forwardDelta    = 0.0f;
	float rightDelta      = 0.0f;
	float mouseDeltaX     = 0.0f;
	float mouseDeltaY     = 0.0f;
	bool  shiftDown       = false;
};

namespace fs = std::filesystem;

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	try
	{
		initializeGfx(LOG_LEVEL_INFO) >> berniniErrChecker;

		auto opts = core::win::WindowOptions{};

		opts.width     = 800;
		opts.height    = 600;
		opts.resizable = false;
		opts.decorated = false;
		opts.mode      = core::win::WindowOptions::Mode::BorderlessWindowed;

		auto wnd = core::win::IWindow::Create(opts);

		game::GfxHandle graphics, camera, scene;

		auto gfxOpts                     = GfxOptions{};
		gfxOpts.wnd.hwnd                 = nullptr;
		gfxOpts.width                    = opts.width;
		gfxOpts.height                   = opts.height;
		gfxOpts.headless                 = false;
		gfxOpts.enableDebugLayer         = true;
		gfxOpts.enableGPUValidationLayer = true;
		gfxOpts.enablePixDebug           = true;

		createGraphics(gfxOpts, &graphics) >> berniniErrChecker;
		auto  mat   = glm::mat4{ 1.0f };
		auto* data  = glm::value_ptr(mat);
		auto  cubes = std::vector<GfxMeshInstance>(3);

		createScene(graphics, &scene) >> berniniErrChecker;

		GfxStaticMesh   cubeMesh   = 0;
		GfxStaticMesh   sphereMesh = 0;
		GfxMeshInstance cubeInstance, sphereInstance;
		GfxMaterial     material1, material2;

		createCubeBase(scene, &cubeMesh) >> berniniErrChecker;
		createSphereBase(scene, &sphereMesh) >> berniniErrChecker;

		auto matOpts          = GfxPBRMaterialOpts{};
		matOpts.albedoColor.r = 1.0f;
		matOpts.albedoColor.g = 1.0f;
		matOpts.albedoColor.b = 1.0f;
		matOpts.alphaMode     = MaterialAlphaMode::AlphaMode_Opaque;
		createPBRMaterial(scene, matOpts, &material1) >> berniniErrChecker;

		auto meshOpts     = GfxStaticMeshOpts{};
		meshOpts.baseMesh = cubeMesh;
		std::copy(data, data + 16, meshOpts.modelTransform);
		meshOpts.material = material1;

		createStaticMeshInstance(scene, meshOpts, &cubeInstance) >> berniniErrChecker;

		mat[3][0] = -5.0f;
		std::copy(data, data + 16, meshOpts.modelTransform);
		meshOpts.baseMesh = sphereMesh;

		createStaticMeshInstance(scene, meshOpts, &sphereInstance) >> berniniErrChecker;

		auto cameraDesc = GfxCameraDesc{ .transform  = { .position = { 0.0f, 0.0f, -20.0f },
			                                             .forward  = { 0.0f, 0.0f, -1.0f } },
			                             .projection = { .fovYDeg     = 60.0f,
			                                             .aspectRatio = 800.0f / 600.0f,
			                                             .nearZ       = 0.5f,
			                                             .farZ        = 500.0f } };
		createCamera(cameraDesc, &camera) >> berniniErrChecker;

		GfxPBRMaterialOpts pbrOpts{};
		pbrOpts.albedoColor.r = 1.0f;
		pbrOpts.albedoColor.g = 0.0f;
		pbrOpts.albedoColor.b = 0.0f;
		pbrOpts.alphaMode     = MaterialAlphaMode::AlphaMode_Blend;

		createPBRMaterial(scene, pbrOpts, &material2) >> berniniErrChecker;
		attachPBRMaterial(scene, sphereInstance, material2) >> berniniErrChecker;

		auto visitor = EventVisitor{};

		for (auto res = wnd->Process(&visitor); res != core::win::IWindow::kClose;
		     res      = wnd->Process(&visitor))
		{
			if (res == core::win::IWindow::kProcess)
			{
				if (visitor.changedPosition)
				{
					cameraMoveAlongView(camera, visitor.forwardDelta) >> berniniErrChecker;
					cameraMoveAlongRight(camera, visitor.rightDelta) >> berniniErrChecker;
				}
				if (visitor.changedRotation)
				{
					cameraRotateYawPitch(camera, visitor.mouseDeltaX, visitor.mouseDeltaY) >>
						berniniErrChecker;
				}
				visitor.Reset();
			}

			drawFrame(graphics, scene, camera) >> berniniErrChecker;
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
