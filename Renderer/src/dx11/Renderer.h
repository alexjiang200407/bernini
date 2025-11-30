#pragma once
#include <Renderer/IRenderer.h>

namespace renderer::dx
{
	class DXRenderer : public IRenderer
	{
	public:
		explicit DXRenderer(const struct RendererOptions& opts);

		void
		DrawFrame() override;

	private:
		wrl::ComPtr<ID3D11Device>        pDevice;
		wrl::ComPtr<ID3D11DeviceContext> pContext;
		wrl::ComPtr<IDXGISwapChain>      pSwap;
	};
}
