#include "Window.h"

HINSTANCE Window::WindowClass::hInstance;

Window::Window()
{
	// Creates window
	hWnd = CreateWindowEx(
		0, WindowClass::wndClassName, L"Bernini", WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,
		NULL,
		WindowClass::hInstance,
		this
	);

	// Check for error
	if (!hWnd)
	{
		return;
	}


	// Shows the window
	ShowWindow(hWnd, SW_SHOWDEFAULT);

}

LRESULT Window::s_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	Window* pWnd;
	if (uMsg == WM_CREATE)
	{
		LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
		pWnd = static_cast<Window*>(lpcs->lpCreateParams);
		SetWindowLongPtr(hWnd, GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>(pWnd));
	}
	else
	{
		pWnd = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}

	if (pWnd)
	{
		return pWnd->WndProc(hWnd, uMsg, wParam, lParam);
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT Window::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

	switch (uMsg)
	{
	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void Window::WindowClass::Register()
{
	hInstance = GetModuleHandle(NULL);

	WNDCLASSEX wcex = {0};
	wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpszClassName = wndClassName;
    wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = s_WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(wcex.hInstance, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = nullptr;
	wcex.lpszMenuName = NULL;
	wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

	RegisterClassEx(&wcex);
}

void Window::WindowClass::Unregister()
{
	UnregisterClass(wndClassName, hInstance);
}
