#pragma once

#include "window/BerniniWin.h"
#include <optional>
#include "input/Keyboard.h"
#include "input/Mouse.h"

class Window
{
public:
	class WindowClass
	{
	public:
		static void Register();
		static void Unregister();

		static constexpr wchar_t WND_CLASS_NAME[] = L"Bernini Window Class";
		static HINSTANCE hInstance;
	};
public:
	Window();
	static LRESULT CALLBACK s_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
	Keyboard kbd;
	Mouse mouse;
	HWND hWnd;
};


