#include <Core/win/Window.h>
#include <cassert>
#include <glfw/glfw3.h>

#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3native.h>

#include <Core/win/KeyEvent.h>
#include <Core/win/MouseEvent.h>

namespace core::win
{
	std::atomic_size_t glfwInitializedCount = 0u;

	Window::Window(const WindowOptions& opts) noexcept
	{
		std::size_t prev = glfwInitializedCount.fetch_add(1);
		assert(prev != std::numeric_limits<std::size_t>::max());
		if (prev == 0)
			glfwInit();

		glfwWindowHint(GLFW_VISIBLE, opts.visible ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_RESIZABLE, opts.resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_DECORATED, opts.decorated ? GLFW_TRUE : GLFW_FALSE);

		GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* vidMode = nullptr;
		if (monitor)
			vidMode = glfwGetVideoMode(monitor);

		int width  = opts.width;
		int height = opts.height;

		// Use video mode dimensions if width/height not specified
		if ((width == 0 || height == 0) && vidMode)
		{
			if (width == 0)
				width = vidMode->width;
			if (height == 0)
				height = vidMode->height;
		}

		// Fallback dimensions
		if (width == 0)
			width = 800;
		if (height == 0)
			height = 600;

		GLFWmonitor* windowMonitor = nullptr;

		switch (opts.mode)
		{
		case WindowOptions::Mode::Windowed:
			break;

		case WindowOptions::Mode::Fullscreen:
			windowMonitor = monitor;
			if (vidMode)
			{
				width  = vidMode->width;
				height = vidMode->height;
			}
			break;

		case WindowOptions::Mode::BorderlessFullscreen:
			windowMonitor = nullptr;
			if (vidMode)
			{
				width  = vidMode->width;
				height = vidMode->height;
			}
			glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
			glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
			break;
		}

		glfwWindow = glfwCreateWindow(width, height, opts.title.data(), windowMonitor, nullptr);
		if (!glfwWindow)
		{
			return;
		}

		if (opts.mode == WindowOptions::Mode::BorderlessFullscreen && monitor && vidMode)
		{
			glfwSetWindowMonitor(
				glfwWindow,
				nullptr,
				0,
				0,
				vidMode->width,
				vidMode->height,
				vidMode->refreshRate);
		}

		glfwSetWindowUserPointer(glfwWindow, this);
		glfwSetKeyCallback(glfwWindow, KeyEvent::GLFWCallback);
		glfwSetCursorPosCallback(glfwWindow, MouseEvent::GLFWCursorPosCallback);
		glfwSetMouseButtonCallback(glfwWindow, MouseEvent::GLFWMouseButtonCallback);
		glfwSetScrollCallback(glfwWindow, MouseEvent::GLFWScrollCallback);

		if (opts.visible)
			glfwShowWindow(glfwWindow);
	}

	void
	Window::Accept(IWindowEventVisitor& visitor) noexcept
	{
		for (auto& evt : queue)
		{
			evt->Accept(visitor);
		}
	}

	Window::~Window() noexcept
	{
		if (glfwWindow)
		{
			glfwDestroyWindow(glfwWindow);
			glfwWindow = nullptr;
		}

		if (glfwInitializedCount.fetch_sub(1) == 1)
			glfwTerminate();
	}

	bool
	Window::PollEvents() const noexcept
	{
		auto shouldContinue = !glfwWindowShouldClose(glfwWindow);
		if (shouldContinue)
		{
			glfwPollEvents();
		}
		return shouldContinue;
	}

	void
	Window::Flush() noexcept
	{
		queue.clear();
	}

}
