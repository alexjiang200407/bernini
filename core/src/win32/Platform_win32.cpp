#include "win32/util.h"
#include "win32/winapi.h"
#include <core/platform/Platform.h>
#include <core/str/str.h>

namespace core
{
	using MouseActions = core::MouseEvent::Actions;
	using MouseAction  = core::MouseEvent::Action;

	class PlatformWin32 : public IPlatform
	{
	public:
		PlatformWin32(const PlatformOptions& options) : IPlatform{ options }
		{
			RegisterWin32(options);
			RegisterRawInput();
		}

		~PlatformWin32()
		{
			if (m_hWnd)
			{
				DestroyWindow(m_hWnd);
				m_hWnd = nullptr;
			}
		}

		void*
		GetNativeHandle() const noexcept
		{
			return m_hWnd;
		}

		void
		RegisterWin32(const PlatformOptions& options)
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
			case PlatformOptions::Mode::Windowed:
				style   = WS_OVERLAPPEDWINDOW;
				exStyle = 0;
				break;

			case PlatformOptions::Mode::BorderlessWindowed:
				style   = WS_POPUP | WS_VISIBLE;
				exStyle = WS_EX_APPWINDOW;
				break;

			case PlatformOptions::Mode::Fullscreen:
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

			ShowCursor(FALSE) >> win32::errorChecker;

			if (options.visible)
				ShowWindow(m_hWnd, SW_SHOW) >> win32::errorChecker;

			UpdateWindow(m_hWnd) >> win32::errorChecker;

			// Has to be after window CreateWindowEx
			if (options.mode != PlatformOptions::Mode::BorderlessWindowed)
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
				auto* const pWnd = reinterpret_cast<PlatformWin32*>(
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
					auto* const pWnd = static_cast<PlatformWin32*>(pCreate->lpCreateParams);

					if (!pWnd->m_hWnd)
					{
						pWnd->m_hWnd = hWnd;
					}

					SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWnd)) >>
						win32::errorChecker;
					SetWindowLongPtr(
						hWnd,
						GWLP_WNDPROC,
						reinterpret_cast<LONG_PTR>(&PlatformWin32::HandleMessageStatic)) >>
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
			case WM_CLOSE:
				PostQuitMessage(0);
				return 0;

			case WM_SETFOCUS:
				if (m_platformMode == PlatformOptions::Mode::BorderlessWindowed)
				{
					RECT rect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
					ClipCursor(&rect) >> win32::errorChecker;
				}
				break;

			case WM_KILLFOCUS:
			{
				Reset();
				break;
			}

			case WM_DESTROY:
				return 0;

			case WM_INPUT:
			{
				UINT dwSize = 0;
				GetRawInputData(
					reinterpret_cast<HRAWINPUT>(lParam),
					RID_INPUT,
					nullptr,
					&dwSize,
					sizeof(RAWINPUTHEADER));
				if (dwSize == 0 || dwSize == static_cast<UINT>(-1))
				{
					throw std::runtime_error("Failed to get Raw input data");
				}

				std::vector<BYTE> lpb(dwSize);
				if (const auto copied = GetRawInputData(
						reinterpret_cast<HRAWINPUT>(lParam),
						RID_INPUT,
						lpb.data(),
						&dwSize,
						sizeof(RAWINPUTHEADER));
				    copied != dwSize || copied == static_cast<UINT>(-1))
				{
					throw std::runtime_error("Failed to get Raw input data");
				}

				RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(lpb.data());

				if (raw->header.dwType == RIM_TYPEMOUSE)
				{
					HandleMouse(raw->data.mouse);
				}
				else if (raw->header.dwType == RIM_TYPEKEYBOARD)
				{
					HandleKeyboard(raw->data.keyboard);
				}
			}
			break;
			default:
				return DefWindowProc(hWnd, uMsg, wParam, lParam);
			}
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
		}

		void
		HandleMouse(RAWMOUSE& rawMouse)
		{
			auto delta   = MouseDelta{};
			auto actions = MouseActions{};

			if (rawMouse.usFlags == MOUSE_MOVE_RELATIVE)
			{
				delta.dx = rawMouse.lLastX;
				delta.dy = rawMouse.lLastY;

				m_mouseState.x += delta.dx;
				m_mouseState.y += delta.dy;

				actions.set(MouseAction::kMove);
			}
			else
			{
				actions.reset(MouseAction::kMove);
			}

			if (rawMouse.usButtonFlags & RI_MOUSE_WHEEL)
			{
				short zDelta = static_cast<short>(rawMouse.usButtonData);
				m_mouseState.wheelPos += zDelta;
				delta.wheelDelta = zDelta;

				actions.set(MouseAction::kWheel);
			}
			else
			{
				actions.reset(MouseAction::kWheel);
			}
			auto updateButton = [&](auto buttonFlag, auto actionPress, auto rawDown, auto rawUp) {
				if (rawDown)
				{
					m_mouseState.flags.set(buttonFlag);
					actions.set(actionPress);
				}

				if (rawUp)
				{
					m_mouseState.flags.reset(buttonFlag);
					actions.reset(actionPress);
				}
			};

			updateButton(
				MouseStateFlagsEnum::kLeftDown,
				MouseAction::kLPress,
				rawMouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN,
				rawMouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP);

			updateButton(
				MouseStateFlagsEnum::kRightDown,
				MouseAction::kRPress,
				rawMouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN,
				rawMouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP);

			updateButton(
				MouseStateFlagsEnum::kMiddleDown,
				MouseAction::kMPress,
				rawMouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN,
				rawMouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP);

			CreateMouseEvent(actions, m_mouseState, delta);
		}

		void
		HandleKeyboard(RAWKEYBOARD& kb)
		{
			bool keyDown = !(kb.Flags & RI_KEY_BREAK);
			UINT vkey    = kb.VKey;

			if (keyDown)
			{
				CreateKeyEvent(vkey, KeyEvent::Type::kPress);

				BYTE keyboardState[256];
				GetKeyboardState(keyboardState) >> win32::errorChecker;

				char16_t buffer[5] = {};
				int      count     = ToUnicode(
					vkey,
					kb.MakeCode,
					keyboardState,
					reinterpret_cast<wchar_t*>(buffer),
					4,
					0);

				if (count)
				{
					std::array<char32_t, 2> utf32 = {};
					size_t retcount = str::toUtf32(std::span{ buffer }, std::span{ utf32 });
					for (size_t i = 0; i < retcount; ++i)
					{
						if (utf32[i] != 0)
						{
							CreateCharEvent(utf32[i]);
						}
					}
				}
			}
			else
			{
				CreateKeyEvent(vkey, KeyEvent::Type::kRelease);
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

	std::unique_ptr<IPlatform>
	IPlatform::Create(const PlatformOptions& options)
	{
		return std::make_unique<PlatformWin32>(options);
	}

}
