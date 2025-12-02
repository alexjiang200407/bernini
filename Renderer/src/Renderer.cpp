#include <Renderer/Renderer.h>
#include <fg/FrameGraph.hpp>
#include <rhi/rhi.h>

//namespace renderer::dx
//{
//	DXRenderer::DXRenderer(const RendererOptions& opts)
//	{
//				constexpr static unsigned int bufferCount = 2u;
//
//				DXGI_SWAP_CHAIN_DESC sd               = {};
//				sd.BufferDesc.Width                   = static_cast<UINT>(opts.width);
//				sd.BufferDesc.Height                  = static_cast<UINT>(opts.height);
//				sd.BufferDesc.Format                  = DXGI_FORMAT_B8G8R8A8_UNORM;
//				sd.BufferDesc.RefreshRate.Numerator   = 0;
//				sd.BufferDesc.RefreshRate.Denominator = 0;
//				sd.BufferDesc.Scaling                 = DXGI_MODE_SCALING_UNSPECIFIED;
//				sd.BufferDesc.ScanlineOrdering        = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
//				sd.SampleDesc.Count                   = 1;
//				sd.SampleDesc.Quality                 = 0;
//				sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
//				sd.BufferCount                        = bufferCount;
//				sd.OutputWindow                       = GetActiveWindow();
//				sd.Windowed                           = TRUE;
//				sd.SwapEffect                         = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
//				sd.Flags                              = 0;
//
//				UINT swapCreateFlags = 0u;
//
//		#ifdef _DEBUG
//				swapCreateFlags |= D3D11_CREATE_DEVICE_DEBUG;
//		#endif
//
//				D3D11CreateDeviceAndSwapChain(
//					nullptr,
//					D3D_DRIVER_TYPE_HARDWARE,
//					nullptr,
//					swapCreateFlags,
//					nullptr,
//					0,
//					D3D11_SDK_VERSION,
//					&sd,
//					&pSwap,
//					&pDevice,
//					nullptr,
//					&pContext) >>
//					dxErrorChecker;
//	}
//
//	void
//	DXRenderer::DrawFrame()
//	{
//		auto frameGraph = FrameGraph{};
//	}
//
//}

namespace renderer
{
	Renderer::Renderer(const RendererOptions& opts)
	{
		(void)opts;
		auto device = rhi::IDevice::Create();
	}

	void
	Renderer::DrawFrame()
	{}
}
