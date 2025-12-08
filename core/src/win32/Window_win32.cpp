#include "win32/Win32Util.h"
#include "win32/WinAPI.h"
#include <Core/except/BerniniException.h>
#include <Core/win/Window.h>

namespace core::win
{
	class WindowWin32 : public IWindow
	{
	public:
		WindowWin32(const WindowOptions& options)
		{
			RegisterWin32(options);
			RegisterRawInput();
		}

		void
		RegisterWin32(const WindowOptions& options)
		{
			static constexpr const char* CLASS_NAME = "Win32WindowClass";

			auto hinstance = GetModuleHandle(nullptr) >> win32::errorChecker;

			WNDCLASSEX wc    = {};
			wc.cbSize        = sizeof(wc);
			wc.lpfnWndProc   = WindowProc;
			wc.hInstance     = hinstance;
			wc.lpszClassName = CLASS_NAME;

			RegisterClassEx(&wc) >> win32::errorChecker;

			DWORD style   = 0;
			DWORD exStyle = 0;
			RECT  rect    = { 0,
				              0,
				              static_cast<LONG>(options.width),
				              static_cast<LONG>(options.height) };

			switch (options.mode)
			{
			case WindowOptions::Mode::Windowed:
				style   = WS_OVERLAPPEDWINDOW;
				exStyle = 0;
				break;

			case WindowOptions::Mode::BorderlessWindowed:
				style   = WS_POPUP | WS_VISIBLE;
				exStyle = WS_EX_APPWINDOW;
				break;

			case WindowOptions::Mode::Fullscreen:
				{
					DEVMODE dm = {};
					ChangeDisplaySettings(&dm, CDS_FULLSCREEN);

					style   = WS_POPUP;
					exStyle = WS_EX_APPWINDOW;

					rect.left   = 0;
					rect.top    = 0;
					rect.right  = GetSystemMetrics(SM_CXSCREEN) >> win32::errorChecker;
					rect.bottom = GetSystemMetrics(SM_CYSCREEN) >> win32::errorChecker;
				}
				break;
			}

			AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
			m_hWnd = CreateWindowEx(
				exStyle,
				CLASS_NAME,
				options.title.data(),
				style,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				rect.right - rect.left,
				rect.bottom - rect.top,
				nullptr,
				nullptr,
				hinstance,
				this);

			if (options.visible)
				ShowWindow(m_hWnd, SW_SHOW) >> win32::errorChecker;

			UpdateWindow(m_hWnd) >> win32::errorChecker;

			// Has to be after window CreateWindowEx
			if (options.mode != WindowOptions::Mode::BorderlessWindowed)
			{
				ClipCursor(&rect) >> win32::errorChecker;
			}
		}

		void
		RegisterRawInput() const
		{
			RAWINPUTDEVICE rid[2] = {};

			// Register mouse
			rid[0].usUsagePage = 0x01;  // Generic Desktop Controls
			rid[0].usUsage     = 0x02;  // Mouse
			rid[0].dwFlags     = 0;
			rid[0].hwndTarget  = m_hWnd;

			// Register keyboard
			rid[1].usUsagePage = 0x01;
			rid[1].usUsage     = 0x06;
			rid[1].dwFlags     = 0;
			rid[1].hwndTarget  = m_hWnd;

			RegisterRawInputDevices(rid, 2, sizeof(rid[0])) >> win32::errorChecker;
		}

		static LRESULT
		HandleMessageStatic(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
		{
			return win32::win32Invoke([=]() -> LRESULT {
				auto* const pWnd = reinterpret_cast<WindowWin32*>(
					GetWindowLongPtr(hWnd, GWLP_USERDATA) >> win32::errorChecker);
				return pWnd->HandleMessage(hWnd, uMsg, wParam, lParam);
			});
		}

		static LRESULT CALLBACK
		WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
		{
			return win32::win32Invoke([=]() -> LRESULT {
				if (uMsg == WM_NCCREATE)
				{
					const CREATESTRUCTW* const pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
					auto* const pWnd = static_cast<WindowWin32*>(pCreate->lpCreateParams);

					SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWnd)) >>
						win32::errorChecker;
					SetWindowLongPtr(
						hWnd,
						GWLP_WNDPROC,
						reinterpret_cast<LONG_PTR>(&WindowWin32::HandleMessageStatic)) >>
						win32::errorChecker;
					return pWnd->HandleMessage(hWnd, uMsg, wParam, lParam);
				}
				return DefWindowProc(hWnd, uMsg, wParam, lParam);
			});
		}

		LRESULT
		HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
		{
			switch (uMsg)
			{
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
			default:
				return DefWindowProc(hWnd, uMsg, wParam, lParam);
			}
		}

		bool
		PollEvents() noexcept override
		{
			MSG msg{};
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
					return false;
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			return true;
		}

		HWND m_hWnd = nullptr;
	};

	std::unique_ptr<IWindow>
	IWindow::Create(const WindowOptions& options) noexcept
	{
		return std::make_unique<WindowWin32>(options);
	}

}
