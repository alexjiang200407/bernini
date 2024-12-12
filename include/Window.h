#pragma once

#include "BerniniWin.h"
#include <optional>

class Window
{
public:
	class WindowClass
	{
	public:
		static void Register();
		static void Unregister();
		static constexpr wchar_t wndClassName[] = L"Bernini Window Class";
		static HINSTANCE hInstance;
	};
public:
	Window();

	static LRESULT CALLBACK s_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
	HWND hWnd;
};


