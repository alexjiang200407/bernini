#include <Core/type_traits.h>
#include <Core/win/Window.h>
#include <concepts>
#include <gfx/GfxHandle.h>
#include <gfx/Vec3.h>
#include <gfx/ffi/gfx.h>
#include <glm/glm.hpp>

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

//struct EventVisitor : public core::win::IWindowEventVisitor
//{
//	void
//	Reset()
//	{
//		changedPosition = false;
//		changedRotation = false;
//		rightDelta      = 0.0f;
//		forwardDelta    = 0.0f;
//		mouseDeltaX     = 0.0f;
//		mouseDeltaY     = 0.0f;
//	}
//
//	void
//	Visit(const core::win::KeyEvent& e, float dt) override
//	{
//		float moveSpeed = 0.2f * dt;
//		if (e.IsReleased())
//		{
//			return;
//		}
//
//		using KeyCode = core::win::KeyCode;
//
//		switch (e.GetKey())
//		{
//		case KeyCode::W:
//			changedPosition = true;
//			forwardDelta -= moveSpeed;
//			break;
//		case KeyCode::A:
//			changedPosition = true;
//			rightDelta -= moveSpeed;
//			break;
//		case KeyCode::S:
//			changedPosition = true;
//			forwardDelta += moveSpeed;
//			break;
//		case KeyCode::D:
//			changedPosition = true;
//			rightDelta += moveSpeed;
//			break;
//		default:
//			break;
//		}
//	}
//
//	void
//	Visit(const core::win::MouseEvent& e, float dt) override
//	{
//		mouseDeltaX += static_cast<float>(e.GetDeltaX()) * dt * 0.005f;
//		mouseDeltaY += static_cast<float>(e.GetDeltaY()) * dt * 0.005f;
//
//		if (std::abs(mouseDeltaX) > 0.0f || std::abs(mouseDeltaY) > 0.0f)
//		{
//			changedRotation = true;
//		}
//	}
//
//	bool  changedPosition = false;
//	bool  changedRotation = false;
//	float forwardDelta    = 0.0f;
//	float rightDelta      = 0.0f;
//	float mouseDeltaX     = 0.0f;
//	float mouseDeltaY     = 0.0f;
//};

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	try
	{
		initializeGfx(LOG_LEVEL_INFO) >> berniniErrChecker;

		auto opts = core::win::WindowOptions{};

		opts.width  = 800;
		opts.height = 600;

		auto wnd = core::win::IWindow::Create(opts);

		game::GfxHandle graphics, camera;

		createGraphics({ .wnd = { .hwnd = nullptr }, .width = 800u, .height = 600u }, &graphics) >>
			berniniErrChecker;

		auto cameraDesc = GfxCameraDesc{ .transform  = { .position = { 0.0f, 0.0f, -20.0f },
			                                             .forward  = { 0.0f, 0.0f, -1.0f } },
			                             .projection = { .fovYDeg     = 60.0f,
			                                             .aspectRatio = 800.0f / 600.0f,
			                                             .nearZ       = 0.5f,
			                                             .farZ        = 500.0f } };
		createCamera(graphics, cameraDesc, &camera) >> berniniErrChecker;

		//auto visitor = EventVisitor{};

		while (wnd->PollEvents())
		{
			//	wnd.Accept(visitor);
			//	wnd.Flush();

			//	if (visitor.changedPosition)
			//	{
			//		cameraMoveAlongView(camera, visitor.forwardDelta) >> berniniErrChecker;
			//		cameraMoveAlongRight(camera, visitor.rightDelta) >> berniniErrChecker;
			//	}
			//	if (visitor.changedRotation)
			//	{
			//		cameraRotateYawPitch(camera, visitor.mouseDeltaX, visitor.mouseDeltaY) >>
			//			berniniErrChecker;
			//	}

			//	drawFrame(graphics, camera) >> berniniErrChecker;

			//	visitor.Reset();
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
