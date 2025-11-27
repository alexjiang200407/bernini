#include <Core/win/Window.h>
#include <cassert>
#include <glfw/glfw3.h>

#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3native.h>

#include "Core/win/KeyEvent.h"
#include <Core/win/MouseEvent.h>

namespace core::win
{
	std::atomic_size_t glfwInitializedCount = 0u;

	Window::Window(int width, int height, std::string_view title) noexcept
	{
		std::size_t prev = glfwInitializedCount.fetch_add(1);
		assert(prev != std::numeric_limits<std::size_t>::max());
		if (prev == 0)
			glfwInit();

		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

		// Auto get width and height if set to 0
		if (width == 0 || height == 0)
		{
			GLFWmonitor* primary = glfwGetPrimaryMonitor();
			if (primary)
			{
				const GLFWvidmode* mode = glfwGetVideoMode(primary);
				if (mode)
				{
					if (width == 0)
						width = mode->width;
					if (height == 0)
						height = mode->height;
				}
			}
		}

		glfwWindow = glfwCreateWindow(width, height, title.data(), nullptr, nullptr);

		glfwSetWindowUserPointer(glfwWindow, this);

		glfwSetKeyCallback(glfwWindow, KeyEvent::GLFWCallback);
		glfwSetCursorPosCallback(glfwWindow, MouseEvent::GLFWCursorPosCallback);
		glfwSetMouseButtonCallback(glfwWindow, MouseEvent::GLFWMouseButtonCallback);
		glfwSetScrollCallback(glfwWindow, MouseEvent::GLFWScrollCallback);

		glfwShowWindow(glfwWindow);
	}

	void
	Window::Accept(IWindowEventVisitor& visitor) noexcept
	{
		for (auto& row : queues)
		{
			for (auto& evt : row)
			{
				evt->Accept(visitor);
			}
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
		for (auto& row : queues)
		{
			row.clear();
		}
	}

}