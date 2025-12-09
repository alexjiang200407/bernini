#include "GfxBase.h"
#include "camera/Camera.h"
#include "ffi/util.h"
#include "geometry/Cube.h"
#include "graphics/Graphics.h"
#include <core/except/BerniniException.h>
#include <gfx/ffi/gfx.h>

namespace
{
	nvrhi::TextureHandle
	CreateDepthTexture(
		const GfxOptions&                           opts,
		nvrhi::RefCountPtr<ID3D11Device>            device,
		nvrhi::DeviceHandle                         m_nvrhiDevice,
		nvrhi::RefCountPtr<ID3D11DepthStencilView>& dsvOut)
	{
		D3D11_TEXTURE2D_DESC depthDesc{};
		depthDesc.Width              = opts.width;
		depthDesc.Height             = opts.height;
		depthDesc.MipLevels          = 1;
		depthDesc.ArraySize          = 1;
		depthDesc.Format             = DXGI_FORMAT_R24G8_TYPELESS;
		depthDesc.SampleDesc.Count   = 1;
		depthDesc.SampleDesc.Quality = 0;
		depthDesc.Usage              = D3D11_USAGE_DEFAULT;
		depthDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		depthDesc.CPUAccessFlags     = 0;
		depthDesc.MiscFlags          = 0;

		nvrhi::RefCountPtr<ID3D11Texture2D> depthTexture;
		device->CreateTexture2D(&depthDesc, nullptr, &depthTexture) >> gfx::dx::dxErrorChecker;

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Texture2D.MipSlice = 0;

		device->CreateDepthStencilView(depthTexture.Get(), &dsvDesc, &dsvOut) >>
			gfx::dx::dxErrorChecker;

		nvrhi::TextureDesc nvrhiDepthDesc;
		nvrhiDepthDesc.width          = opts.width;
		nvrhiDepthDesc.height         = opts.height;
		nvrhiDepthDesc.sampleCount    = 1;
		nvrhiDepthDesc.sampleQuality  = 0;
		nvrhiDepthDesc.format         = nvrhi::Format::D24S8;
		nvrhiDepthDesc.debugName      = "DepthStencil";
		nvrhiDepthDesc.isRenderTarget = true;
		nvrhiDepthDesc.isUAV          = false;

		return m_nvrhiDevice->createHandleForNativeTexture(
			nvrhi::ObjectTypes::D3D11_Resource,
			nvrhi::Object{ depthTexture.Get() },
			nvrhiDepthDesc);
	}
}

namespace gfx
{
	class Graphics : public IGraphics
	{
	public:
		Graphics(const GfxOptions& opts);
		~Graphics();

		void
		DrawFrame(Camera& camera) override;

	private:
		nvrhi::RefCountPtr<IDXGISwapChain>         m_swap;
		nvrhi::RefCountPtr<ID3D11DeviceContext>    m_context;
		nvrhi::RefCountPtr<ID3D11Device>           m_device;
		nvrhi::RefCountPtr<ID3D11Texture2D>        m_backBuffer;
		nvrhi::RefCountPtr<ID3D11DepthStencilView> m_depthBuffer;
		nvrhi::TextureHandle                       m_nvrhiDepthBuffer;
		nvrhi::TextureHandle                       m_nvrhiBackBuffer;
		nvrhi::FramebufferInfo                     m_framebufferInfo;
		std::unique_ptr<geom::Cube>                cube;
	};

	Graphics::Graphics(const GfxOptions& opts)
	{
		constexpr static unsigned int bufferCount = 2u;

		windowHeight = opts.height;
		windowWidth  = opts.width;

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
			&m_swap,
			&m_device,
			nullptr,
			&m_context) >>
			gfx::dx::dxErrorChecker;

		m_nvrhiDevice = nvrhi::d3d11::createDevice({ .context = m_context });

		cube = std::make_unique<geom::Cube>(m_nvrhiDevice);

		m_swap->GetBuffer(0, IID_PPV_ARGS(&m_backBuffer)) >> gfx::dx::dxErrorChecker;

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

			m_nvrhiBackBuffer = m_nvrhiDevice->createHandleForNativeTexture(
				nvrhi::ObjectTypes::D3D11_Resource,
				nvrhi::Object{ m_backBuffer.Get() },
				textureDesc);

			m_nvrhiDepthBuffer = CreateDepthTexture(opts, m_device, m_nvrhiDevice, m_depthBuffer);
			nvrhiFramebuffer =
				m_nvrhiDevice->createFramebuffer(nvrhi::FramebufferDesc{}
			                                         .addColorAttachment(m_nvrhiBackBuffer)
			                                         .setDepthAttachment(m_nvrhiDepthBuffer));

			m_framebufferInfo = nvrhi::FramebufferInfo{}.addColorFormat(nvrhi::Format::BGRA8_UNORM);
		}
	}

	Graphics::~Graphics() {}

	void
	Graphics::DrawFrame(gfx::Camera& camera)
	{
		nvrhi::CommandListHandle commandList = m_nvrhiDevice->createCommandList();
		nvrhi::utils::ClearColorAttachment(
			commandList,
			nvrhiFramebuffer,
			0,
			nvrhi::Color{ 0.0f, 0.0f, 0.0f, 1.0f });
		nvrhi::utils::ClearDepthStencilAttachment(commandList, nvrhiFramebuffer, 1.0f, 0);

		camera.UpdateBuffer(commandList);

		auto renderState = nvrhi::RenderState{}
		                       .setRasterState(nvrhi::RasterState{}
		                                           .setCullMode(nvrhi::RasterCullMode::None)
		                                           .setFillMode(nvrhi::RasterFillMode::Solid))
		                       .setDepthStencilState(nvrhi::DepthStencilState{}
		                                                 .setDepthTestEnable(true)
		                                                 .setDepthWriteEnable(true)
		                                                 .setDepthFunc(nvrhi::ComparisonFunc::Less)
		                                                 .setStencilEnable(false));

		// Per-Material
		auto pipelineDesc = nvrhi::GraphicsPipelineDesc{}
		                        .addBindingLayout(camera.GetBindingLayout())
		                        .setVertexShader(cube->vertexShader)
		                        .setPixelShader(cube->pixelShader)
		                        .setInputLayout(cube->GetInputLayout())
		                        .setPrimType(nvrhi::PrimitiveType::TriangleList)
		                        .setRenderState(renderState);

		nvrhi::GraphicsPipelineHandle graphicsPipeline =
			m_nvrhiDevice->createGraphicsPipeline(pipelineDesc, m_framebufferInfo);

		auto                 cameraBindingSet = camera.GetBindingSet(m_nvrhiDevice);
		nvrhi::GraphicsState globalGraphicsState =
			nvrhi::GraphicsState{}
				.setPipeline(graphicsPipeline)
				.setFramebuffer(nvrhiFramebuffer)
				.addBindingSet(cameraBindingSet)
				.setViewport(nvrhi::ViewportState{}.addViewportAndScissorRect(
					nvrhi::Viewport{ static_cast<float>(windowWidth),
		                             static_cast<float>(windowHeight) }));

		cube->Draw(commandList, globalGraphicsState);

		commandList->close();
		m_nvrhiDevice->executeCommandList(commandList);

		m_swap->Present(1, 0);
	}
}

GfxResult
createGraphics(GfxOptions options, Gfx* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		gfx::ffi::validatePtr(out, "out");
		out->destroy = gfx::ffi::deleteThunk;

		out->data = new gfx::Graphics(options);
		return GFX_RESULT_OK;
	});
}
