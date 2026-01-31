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

#include <core/file/file.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

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
	Visit(const core::win::CharEvent& e) override
	{
		spdlog::info("Char Event: U+{:04X}", static_cast<uint32_t>(e.GetChar()));
	}

	void
	Visit(const core::win::KeyEvent& e, float dt) override
	{
		float moveSpeed = dt * 2.0f;

		if (e.GetKeyCode() == 16)  // Shift
		{
			shiftDown = e.IsHeld();
		}

		if (e.IsPress())
		{
			switch (e.GetKeyCode())
			case 49:  // 1 key
			case 50:  // 2 key
			case 51:  // 3 key
			{
				if (e.IsPress())
				{
					auto idx = e.GetKeyCode() - 49;
					if (meshes[idx])
					{
						destroyMesh(gfx, meshes[idx]) >> berniniErrChecker;
						meshes[idx] = 0;
					}
					else
					{
						auto mat  = glm::mat4{ 1.0f };
						mat[3][0] = static_cast<float>(idx) * -5.0f;

						auto* data = glm::value_ptr(mat);

						if (idx == 1)
						{
							createSphere(gfx, data, &meshes[idx]) >> berniniErrChecker;
						}
						else
						{
							createCube(gfx, data, &meshes[idx]) >> berniniErrChecker;
						}
					}
				}

				break;
			}
		}

		if (!e.IsHeld())
		{
			return;
		}
		spdlog::info("Key Event: KeyCode={}", static_cast<int>(e.GetKeyCode()));

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

		for (const auto& info : allActions)
		{
			if (actions.All(info.flag))
			{
				spdlog::info("Mouse Action: {}", info.name);
			}
		}
	}

	bool                 changedPosition = false;
	bool                 changedRotation = false;
	float                forwardDelta    = 0.0f;
	float                rightDelta      = 0.0f;
	float                mouseDeltaX     = 0.0f;
	float                mouseDeltaY     = 0.0f;
	bool                 shiftDown       = false;
	std::vector<GfxMesh> meshes;
	Gfx                  gfx;
};

namespace fs = std::filesystem;

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	try
	{
		{
			auto     libraryPath = core::file::getLibraryPath();
			fs::path logPath     = libraryPath.parent_path() / "game.log";

			auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);

			auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

			log->set_level(spdlog::level::info);
			log->flush_on(spdlog::level::info);

			spdlog::set_default_logger(std::move(log));
			spdlog::set_pattern("[%H:%M:%S:%e] [thread %t] [%l] %v"s);
		}

		initializeGfx(LOG_LEVEL_INFO) >> berniniErrChecker;

		auto opts = core::win::WindowOptions{};

		opts.width  = 800;
		opts.height = 600;

		auto wnd = core::win::IWindow::Create(opts);

		game::GfxHandle graphics, camera;

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
		auto  cubes = std::vector<GfxMesh>(3);

		createCube(graphics, data, &cubes[0]) >> berniniErrChecker;

		mat[3][0] = -5.0f;

		createSphere(graphics, data, &cubes[1]) >> berniniErrChecker;

		mat[3][0] = -10.0f;

		createCube(graphics, data, &cubes[2]) >> berniniErrChecker;

		auto cameraDesc = GfxCameraDesc{ .transform  = { .position = { 0.0f, 0.0f, -20.0f },
			                                             .forward  = { 0.0f, 0.0f, -1.0f } },
			                             .projection = { .fovYDeg     = 60.0f,
			                                             .aspectRatio = 800.0f / 600.0f,
			                                             .nearZ       = 0.5f,
			                                             .farZ        = 500.0f } };
		createCamera(cameraDesc, &camera) >> berniniErrChecker;

		auto visitor   = EventVisitor{};
		visitor.meshes = std::move(cubes);
		visitor.gfx    = graphics;

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

			drawFrame(graphics, camera) >> berniniErrChecker;
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
