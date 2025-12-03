#include "GfxBase.h"
#include "RendererException.h"
#include "dx11/DXError.h"
#include "ffiUtil.h"
#include <Core/except/BerniniException.h>
#include <gfx/Renderer.h>

namespace gfx
{
	class Renderer : public GfxBase
	{
	public:
		Renderer(const Bernini_RendererOptions& opts);
		~Renderer();

		void
		DrawFrame();

	private:
		nvrhi::RefCountPtr<IDXGISwapChain>      swap;
		nvrhi::RefCountPtr<ID3D11DeviceContext> context;
		nvrhi::RefCountPtr<ID3D11Device>        device;
		nvrhi::RefCountPtr<ID3D11Texture2D>     backBuffer;
		nvrhi::TextureHandle                    nvrhiBackBuffer;
		nvrhi::DeviceHandle                     nvrhiDevice;
		nvrhi::FramebufferHandle                nvrhiFramebuffer;
	};

	Renderer::Renderer(const Bernini_RendererOptions& opts)
	{
		constexpr static unsigned int bufferCount = 2u;

		DXGI_SWAP_CHAIN_DESC sd               = {};
		sd.BufferDesc.Width                   = static_cast<UINT>(opts.width);
		sd.BufferDesc.Height                  = static_cast<UINT>(opts.height);
		sd.BufferDesc.Format                  = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator   = 0;
		sd.BufferDesc.RefreshRate.Denominator = 0;
		sd.BufferDesc.Scaling                 = DXGI_MODE_SCALING_UNSPECIFIED;
		sd.BufferDesc.ScanlineOrdering        = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		sd.SampleDesc.Count                   = 1;
		sd.SampleDesc.Quality                 = 0;
		sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount                        = bufferCount;
		sd.Windowed                           = TRUE;
		sd.SwapEffect                         = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		sd.Flags                              = 0;

		sd.OutputWindow = opts.wnd.hwnd ? static_cast<HWND>(opts.wnd.hwnd) : GetActiveWindow();

		UINT swapCreateFlags = 0u;

#ifdef _DEBUG
		swapCreateFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			swapCreateFlags,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			&sd,
			&swap,
			nullptr,
			nullptr,
			&context) >>
			gfx::dx::dxErrorChecker;

		nvrhiDevice = nvrhi::d3d11::createDevice({
			.context = context,
		});

		swap->GetBuffer(0, IID_PPV_ARGS(&backBuffer)) >> gfx::dx::dxErrorChecker;

		{
			nvrhi::TextureDesc textureDesc;
			textureDesc.width          = sd.BufferDesc.Width;
			textureDesc.height         = sd.BufferDesc.Height;
			textureDesc.sampleCount    = sd.SampleDesc.Count;
			textureDesc.sampleQuality  = sd.SampleDesc.Quality;
			textureDesc.format         = nvrhi::Format::BGRA8_UNORM;
			textureDesc.debugName      = "SwapChainBuffer";
			textureDesc.isRenderTarget = true;
			textureDesc.isUAV          = false;

			nvrhiBackBuffer = nvrhiDevice->createHandleForNativeTexture(
				nvrhi::ObjectTypes::D3D11_Resource,
				nvrhi::Object{ backBuffer.Get() },
				textureDesc);

			nvrhiFramebuffer = nvrhiDevice->createFramebuffer(
				nvrhi::FramebufferDesc{}.addColorAttachment(nvrhiBackBuffer));
		}
	}

	Renderer::~Renderer() {}

	void
	Renderer::DrawFrame()
	{
		nvrhi::CommandListHandle commandList = nvrhiDevice->createCommandList();
		nvrhi::utils::ClearColorAttachment(
			commandList,
			nvrhiFramebuffer,
			0,
			nvrhi::Color{ 1.0f, 0.0f, 0.0f, 1.0f });

		commandList->close();
		nvrhiDevice->executeCommandList(commandList);

		swap->Present(1, 0);
	}
}

Bernini_GfxResult
bernini_createRenderer(Bernini_RendererOptions options, Bernini_GfxObj* out)
{
	try
	{
		if (out == nullptr)
		{
			throw gfx::RendererException{ BERNINI_GFX_RENDERER_RESULT_ERROR_INVALID_ARGUMENT,
				                          "Invalid Argument",
				                          "<out> cannot be nullptr" };
		}
		auto renderer = new gfx::Renderer{ options };
		out->destroy  = gfx::ffi::handleDeleteThunk<gfx::Renderer>;

		out->data = renderer;
		return BERNINI_GFX_RENDERER_RESULT_OK;
	}
	catch (const gfx::RendererException& ex)
	{
		return ex.GetErrorResult();
	}
	catch (...)
	{
		return BERNINI_GFX_RENDERER_RESULT_ERROR_UNKNOWN;
	}
}

Bernini_GfxResult
bernini_drawFrame(Bernini_GfxObj renderer)
{
	try
	{
		auto* ren = gfx::ffi::gfxObjCast<gfx::Renderer>(renderer);
		ren->DrawFrame();
		return BERNINI_GFX_RENDERER_RESULT_OK;
	}
	catch (const gfx::RendererException& ex)
	{
		return ex.GetErrorResult();
	}
	catch (...)
	{
		return BERNINI_GFX_RENDERER_RESULT_ERROR_UNKNOWN;
	}
}
