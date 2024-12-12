#include "BerniniWin.h"
#include "Window.h"
#include <d3d11.h>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, PSTR pCmdLine, int nCmdShow)
{
	Window::WindowClass::Register();

	Window wnd;

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	Window::WindowClass::Unregister();

	return 0;
}

