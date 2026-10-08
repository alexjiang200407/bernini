// bgl_tests globs every .cpp under tests/ whatever the backend, so a Vulkan-only case has to exclude
// itself: an HWND's swapchain is the Vulkan backend's window here.
#if defined(RENDERER_BACKEND_VULKAN)

#	include "util/GpuValidation.h"
#	include "util/TestGraphics.h"
#	include "util/TestOptions.h"
#	include <Windows.h>
#	include <bgl/IGraphics.h>
#	include <bgl/IRenderTarget.h>
#	include <catch2/catch_test_macros.hpp>

namespace
{
	// Borderless, so the client area -- the extent the swapchain takes -- is the window's whole size.
	class HiddenWindow
	{
	public:
		HiddenWindow() :
			m_Hwnd(CreateWindowExW(
				0,
				L"STATIC",
				L"bgl_tests",
				WS_POPUP,
				0,
				0,
				64,
				32,
				nullptr,
				nullptr,
				GetModuleHandleW(nullptr),
				nullptr))
		{}

		~HiddenWindow() noexcept { DestroyWindow(m_Hwnd); }

		HiddenWindow(const HiddenWindow&) = delete;
		HiddenWindow&
		operator=(const HiddenWindow&) = delete;

		[[nodiscard]] HWND
		Get() const noexcept
		{
			return m_Hwnd;
		}

	private:
		HWND m_Hwnd;
	};

	void
	PresentFrames(HWND hwnd)
	{
		auto opts                                = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer         = true;
		opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::test::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto desc     = bgl::RenderTargetDesc();
		desc.width    = 64;
		desc.height   = 32;
		desc.headless = false;
		desc.wnd      = hwnd;

		auto target = gfx->CreateRenderTarget(desc);
		REQUIRE(target != nullptr);

		for (int frame = 0; frame < 4; ++frame)
		{
			gfx->BeginFrame(target);
			gfx->EndFrame();
		}
		gfx->WaitIdle();
	}
}

// Every other case renders headless, so this is the one that reaches the swapchain, and with it the
// Vulkan functions bgl calls through its own copy of volk's pointers. The second context is a new
// device: pointers left at the first one's would call into a destroyed device.
TEST_CASE(
	"A windowed target presents on Vulkan, and again on a second context",
	"[present][graphics][vulkan]")
{
	const auto window = HiddenWindow();
	REQUIRE(window.Get() != nullptr);
	auto client = RECT();
	REQUIRE(GetClientRect(window.Get(), &client));
	REQUIRE(client.right == 64);
	REQUIRE(client.bottom == 32);

	PresentFrames(window.Get());
	PresentFrames(window.Get());
}

#endif
