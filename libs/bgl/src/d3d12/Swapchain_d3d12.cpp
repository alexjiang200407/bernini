#include "swapchain/ISwapchain.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/device/Device.h>
#include <bgpu/resource/NativeTextureDesc.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/NativeObject.h>
#include <bgpu/types/TextureDimension.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
	using bgpu::d3d12ErrChecker;
}

namespace bgl
{
	namespace
	{
		class DxgiSwapchain final : public ISwapchain
		{
		public:
			DxgiSwapchain(
				const RenderTargetDesc&      desc,
				const bgpu::CommandQueueRef& queue,
				const bool                   enableDebug)
			{
				HWND hwnd = desc.wnd ? static_cast<HWND>(desc.wnd) : GetActiveWindow();

				DXGI_SWAP_CHAIN_DESC1 sd = {};
				sd.Width                 = static_cast<UINT>(desc.width);
				sd.Height                = static_cast<UINT>(desc.height);
				sd.Format                = c_Format;
				sd.BufferCount           = c_ImageCount;
				sd.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				sd.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				sd.Scaling               = DXGI_SCALING_STRETCH;
				sd.AlphaMode             = DXGI_ALPHA_MODE_IGNORE;
				sd.SampleDesc.Count      = 1;

				wrl::ComPtr<IDXGIFactory4> factory;
				const UINT factoryFlags = enableDebug ? DXGI_CREATE_FACTORY_DEBUG : 0;
				CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)) >> d3d12ErrChecker;

				wrl::ComPtr<IDXGISwapChain1> swap;

				auto* d3d12CommandQueue =
					queue->GetNativeObject(bgpu::NativeObjectType::kD3D12CommandQueue)
						.As<ID3D12CommandQueue>();
				factory->CreateSwapChainForHwnd(
					d3d12CommandQueue,
					hwnd,
					&sd,
					nullptr,
					nullptr,
					&swap) >>
					d3d12ErrChecker;

				swap->QueryInterface(IID_PPV_ARGS(&m_SwapChain)) >> d3d12ErrChecker;
				// NO_WINDOW_CHANGES stops DXGI hooking the window's message queue. Without it a
				// Present issued from a thread other than the window's can deadlock against that
				// queue, and bgl resizes the swapchain itself rather than letting DXGI respond to
				// window changes.
				factory->MakeWindowAssociation(
					hwnd,
					DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER) >>
					d3d12ErrChecker;

				m_Width  = sd.Width;
				m_Height = sd.Height;
			}

			~DxgiSwapchain() noexcept override { m_SwapChain->SetFullscreenState(FALSE, nullptr); }

			DxgiSwapchain(const DxgiSwapchain&) = delete;
			DxgiSwapchain(DxgiSwapchain&&)      = delete;
			DxgiSwapchain&
			operator=(const DxgiSwapchain&) = delete;
			DxgiSwapchain&
			operator=(DxgiSwapchain&&) = delete;

			[[nodiscard]] std::vector<bgpu::NativeTextureDesc>
			GetImages() const override
			{
				auto textureDesc          = bgpu::TextureDesc();
				textureDesc.format        = bgpu::Format::BGRA8_UNORM;
				textureDesc.width         = m_Width;
				textureDesc.height        = m_Height;
				textureDesc.dimension     = bgpu::TextureDimension::kTexture2D;
				textureDesc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
				textureDesc.initialLayout = bgpu::BarrierLayout::kPresent;

				auto images = std::vector<bgpu::NativeTextureDesc>();
				for (UINT i = 0; i < c_ImageCount; i++)
				{
					// The swapchain holds every buffer, so the pointer outlives this reference.
					wrl::ComPtr<ID3D12Resource> buffer;
					m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&buffer)) >> d3d12ErrChecker;
					images.push_back(
						bgpu::NativeTextureDesc()
							.SetObject(
								bgpu::NativeObjectType::kD3D12Resource,
								bgpu::NativeObject{ buffer.Get() })
							.SetTexture(textureDesc));
				}
				return images;
			}

			[[nodiscard]] bgpu::Format
			GetViewFormat() const noexcept override
			{
				return bgpu::Format::SBGRA8_UNORM;
			}

			[[nodiscard]] uint32_t
			GetCurrentImage() const noexcept override
			{
				return m_SwapChain->GetCurrentBackBufferIndex();
			}

			[[nodiscard]] bool
			StartsUndefined() const noexcept override
			{
				return false;
			}

			[[nodiscard]] bool
			CanReadPresented() const noexcept override
			{
				return true;
			}

			// DXGI orders the present behind every submission already on the queue it was made on,
			// and scales the buffers to whatever the window has become.
			[[nodiscard]] bool
			Present(const uint64_t frameFence) noexcept override
			{
				(void)frameFence;
				m_SwapChain->Present(1, 0) >> d3d12ErrChecker;
				return false;
			}

			void
			Resize(const uint32_t width, const uint32_t height) override
			{
				m_SwapChain->ResizeBuffers(c_ImageCount, width, height, c_Format, 0) >>
					d3d12ErrChecker;
				m_Width  = width;
				m_Height = height;
			}

		private:
			static constexpr UINT        c_ImageCount = 2;
			static constexpr DXGI_FORMAT c_Format     = DXGI_FORMAT_B8G8R8A8_UNORM;

			wrl::ComPtr<IDXGISwapChain3> m_SwapChain;
			uint32_t                     m_Width  = 0;
			uint32_t                     m_Height = 0;
		};
	}

	std::unique_ptr<ISwapchain>
	CreateBackendSwapchain(
		const RenderTargetDesc&      desc,
		const bgpu::DeviceRef&       device,
		const bgpu::CommandQueueRef& queue,
		const bool                   enableDebug)
	{
		(void)device;
		return std::make_unique<DxgiSwapchain>(desc, queue, enableDebug);
	}
}
