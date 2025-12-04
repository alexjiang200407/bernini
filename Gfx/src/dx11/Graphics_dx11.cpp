#include "GfxBase.h"
#include "ffi/util.h"
#include "graphics/Graphics.h"
#include <Core/except/BerniniException.h>
#include <gfx/gfx.h>

namespace gfx
{
	class Graphics : public IGraphics
	{
	public:
		Graphics(const GraphicsOptions& opts);
		~Graphics();

		void
		DrawFrame();

	private:
		nvrhi::RefCountPtr<IDXGISwapChain>      swap;
		nvrhi::RefCountPtr<ID3D11DeviceContext> context;
		nvrhi::RefCountPtr<ID3D11Device>        device;
		nvrhi::RefCountPtr<ID3D11Texture2D>     backBuffer;
		nvrhi::TextureHandle                    nvrhiBackBuffer;
	};

	Graphics::Graphics(const GraphicsOptions& opts)
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

	Graphics::~Graphics() {}

	void
	Graphics::DrawFrame()
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

GfxResult
createGraphics(GraphicsOptions options, Graphics* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		gfx::ffi::validatePtr(out, "out");
		out->destroy = gfx::ffi::deleteThunk;

		out->data = new gfx::Graphics(options);
		return GFX_RESULT_OK;
	});
}
