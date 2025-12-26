#include "camera/Camera.h"
#include "drawable/Drawable.h"
#include "ffi/util.h"
#include "graphics/Graphics.h"
#include "material/Material.h"
#include "mesh/MeshFactory.h"
#include "passes/GBufferPass.h"
#include <fg/Blackboard.hpp>
#include <nvrhi/validation.h>

#ifdef _DEBUG
#	include <dxgidebug.h>
#endif

namespace gfx
{
	class ErrorCallback : public nvrhi::IMessageCallback
	{
		void
		message(nvrhi::MessageSeverity severity, const char* messageText) override
		{
			switch (severity)
			{
			case nvrhi::MessageSeverity::Info:
				logger::warn("NVRHI Info: {}", messageText);
				break;
			case nvrhi::MessageSeverity::Warning:
				logger::warn("NVRHI Warning: {}", messageText);
				break;
			case nvrhi::MessageSeverity::Error:
				logger::error("NVRHI Error: {}", messageText);
				break;
			case nvrhi::MessageSeverity::Fatal:
				logger::critical("NVRHI Fatal Error: {}", messageText);
				break;
			}
		}
	};

	class Graphics : public IGraphics
	{
	public:
		Graphics(const GfxOptions& opts);
		~Graphics();

		void
		DrawFrame(Camera& camera) override;

	private:
		void
		CreateDevice();
		void
		CreateSwapChain(HWND hwnd);
		void
		CreateDepthBuffer();
		void
		CreateRenderTargets();
		void
		ReleaseRenderTargets();

	private:
		nvrhi::RefCountPtr<ID3D12Device>                m_device;
		nvrhi::RefCountPtr<ID3D12CommandQueue>          m_commandQueue;
		nvrhi::RefCountPtr<IDXGISwapChain3>             m_swapChain;
		nvrhi::RefCountPtr<ID3D12Resource>              m_depthBuffer;
		std::vector<nvrhi::RefCountPtr<ID3D12Resource>> m_backBuffers;

#ifdef _DEBUG
		nvrhi::RefCountPtr<ID3D12Debug>     m_debugController;
		nvrhi::RefCountPtr<IDXGIInfoQueue>  m_dxgiInfoQueue;
		nvrhi::RefCountPtr<ID3D12InfoQueue> m_infoQueue;
#endif

		nvrhi::DeviceHandle               m_nvrhiDevice;
		nvrhi::FramebufferHandle          m_nvrhiFramebuffer;
		nvrhi::TextureHandle              m_nvrhiDepthBuffer;
		std::vector<nvrhi::TextureHandle> m_nvrhiBackBuffers;
		nvrhi::FramebufferInfo            m_framebufferInfo;

		std::unique_ptr<MeshFactory>           m_meshFactory;
		GBufferPass                            m_gBufferPass;
		std::vector<std::unique_ptr<Drawable>> m_drawable;

		bool m_isHeadless;
		UINT m_windowWidth;
		UINT m_windowHeight;
		UINT m_frameCount = 0;

		std::vector<HANDLE>             m_frameFenceEvents;
		nvrhi::RefCountPtr<ID3D12Fence> m_frameFence;
		ErrorCallback                   m_errorCB;
	};

	Graphics::Graphics(const GfxOptions& opts)
	{
		m_isHeadless   = opts.headless;
		m_windowWidth  = opts.width;
		m_windowHeight = opts.height;

#ifdef _DEBUG
		{
			D3D12GetDebugInterface(IID_PPV_ARGS(&m_debugController)) >> gfx::dx::dxErrorChecker;
			m_debugController->EnableDebugLayer();
		}
#endif

		CreateDevice();

#ifdef _DEBUG
		DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_dxgiInfoQueue)) >> gfx::dx::dxErrorChecker;

		m_dxgiInfoQueue->SetBreakOnSeverity(
			DXGI_DEBUG_ALL,
			DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,
			TRUE);

		m_dxgiInfoQueue->SetBreakOnSeverity(
			DXGI_DEBUG_ALL,
			DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,
			TRUE);
#endif

		if (!m_isHeadless)
		{
			auto hwnd = opts.wnd.hwnd ? static_cast<HWND>(opts.wnd.hwnd) : GetActiveWindow();
			CreateSwapChain(hwnd);
		}

		CreateDepthBuffer();

		auto deviceDesc                  = nvrhi::d3d12::DeviceDesc{};
		deviceDesc.pDevice               = m_device.Get();
		deviceDesc.pGraphicsCommandQueue = m_commandQueue.Get();
		deviceDesc.errorCB               = &m_errorCB;
		m_nvrhiDevice                    = nvrhi::d3d12::createDevice(deviceDesc);

#ifdef _DEBUG
		{
			m_nvrhiDevice = nvrhi::validation::createValidationLayer(m_nvrhiDevice);
		}
#endif

		if (!m_isHeadless)
			CreateRenderTargets();

		m_meshFactory = std::make_unique<MeshFactory>(m_nvrhiDevice);
		auto drawable = std::make_unique<Drawable>(ShaderMatrix{});
		drawable->SetMesh(m_meshFactory->CreateSphere("shaders/VS_cube.cso"sv));
		drawable->SetMaterial(std::make_shared<Material>(m_nvrhiDevice, "shaders/PS_cube.cso"sv));
		m_drawable.push_back(std::move(drawable));

		GeomPass::Setup(m_nvrhiDevice);
	}

	Graphics::~Graphics()
	{
		ReleaseRenderTargets();
		GeomPass::Shutdown();
	}

	void
	Graphics::CreateDevice()
	{
		D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)) >>
			gfx::dx::dxErrorChecker;

		D3D12_COMMAND_QUEUE_DESC cqDesc{};
		cqDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
		cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		m_device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&m_commandQueue)) >>
			gfx::dx::dxErrorChecker;

#ifdef _DEBUG
		m_device->QueryInterface(IID_PPV_ARGS(&m_infoQueue));
		if (m_infoQueue)
			m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
#endif
	}

	void
	Graphics::CreateSwapChain(HWND hwnd)
	{
		DXGI_SWAP_CHAIN_DESC1 sd{};
		sd.Width       = m_windowWidth;
		sd.Height      = m_windowHeight;
		sd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.BufferCount = 2;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.Scaling     = DXGI_SCALING_STRETCH;
		sd.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;

		sd.SampleDesc.Count   = 1;
		sd.SampleDesc.Quality = 0;

		nvrhi::RefCountPtr<IDXGIFactory4> factory;

#ifdef _DEBUG
		CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)) >>
			gfx::dx::dxErrorChecker;
#else
		CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)) >> gfx::dx::dxErrorChecker;
#endif  // DEBUG

		nvrhi::RefCountPtr<IDXGISwapChain1> swap;
		factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &sd, nullptr, nullptr, &swap) >>
			gfx::dx::dxErrorChecker;

		swap->QueryInterface(IID_PPV_ARGS(&m_swapChain));

		factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER) >> gfx::dx::dxErrorChecker;
	}

	void
	Graphics::CreateDepthBuffer()
	{
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width            = m_windowWidth;
		desc.Height           = m_windowHeight;
		desc.DepthOrArraySize = 1;
		desc.MipLevels        = 1;
		desc.Format           = DXGI_FORMAT_D24_UNORM_S8_UINT;
		desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.SampleDesc.Count = 1;

		D3D12_HEAP_PROPERTIES heapProps{};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_CLEAR_VALUE clear{};
		clear.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
		clear.DepthStencil.Depth = 1.0f;

		m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clear,
			IID_PPV_ARGS(&m_depthBuffer)) >>
			gfx::dx::dxErrorChecker;
	}

	void
	Graphics::CreateRenderTargets()
	{
		constexpr UINT bufferCount = 2;
		m_backBuffers.resize(bufferCount);
		m_nvrhiBackBuffers.resize(bufferCount);

		nvrhi::TextureDesc texDesc;
		texDesc.width          = m_windowWidth;
		texDesc.height         = m_windowHeight;
		texDesc.sampleCount    = 1;
		texDesc.format         = nvrhi::Format::BGRA8_UNORM;
		texDesc.isRenderTarget = true;

		for (UINT i = 0; i < bufferCount; i++)
		{
			m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
			m_nvrhiBackBuffers[i] = m_nvrhiDevice->createHandleForNativeTexture(
				nvrhi::ObjectTypes::D3D12_Resource,
				m_backBuffers[i].Get(),
				texDesc);
		}

		nvrhi::TextureDesc depthDesc;
		depthDesc.width          = m_windowWidth;
		depthDesc.height         = m_windowHeight;
		depthDesc.format         = nvrhi::Format::D24S8;
		depthDesc.isRenderTarget = true;

		m_nvrhiDepthBuffer = m_nvrhiDevice->createHandleForNativeTexture(
			nvrhi::ObjectTypes::D3D12_Resource,
			m_depthBuffer.Get(),
			depthDesc);

		m_nvrhiFramebuffer = m_nvrhiDevice->createFramebuffer(
			nvrhi::FramebufferDesc{}
				.addColorAttachment(m_nvrhiBackBuffers[0])
				.setDepthAttachment(m_nvrhiDepthBuffer));

		m_framebufferInfo = nvrhi::FramebufferInfo{}.addColorFormat(nvrhi::Format::BGRA8_UNORM);
	}

	void
	Graphics::ReleaseRenderTargets()
	{
		m_nvrhiBackBuffers.clear();
		m_backBuffers.clear();
		m_nvrhiFramebuffer = nullptr;
		m_nvrhiDepthBuffer = nullptr;
	}

	void
	Graphics::DrawFrame(Camera& camera)
	{
		auto cmdList = m_nvrhiDevice->createCommandList();
		nvrhi::utils::ClearColorAttachment(
			cmdList,
			m_nvrhiFramebuffer,
			0,
			nvrhi::Color{ 0, 0, 0, 1 });
		nvrhi::utils::ClearDepthStencilAttachment(cmdList, m_nvrhiFramebuffer, 1.0f, 0);
		cmdList->close();
		m_nvrhiDevice->executeCommandList(cmdList);

		FrameGraph           fg;
		FrameGraphBlackboard blackboard;
		RenderArgs           args{ .screenWidth   = static_cast<float>(m_windowWidth),
			                       .screenHeight  = static_cast<float>(m_windowHeight),
			                       .device        = m_nvrhiDevice,
			                       .outBuffer     = m_nvrhiFramebuffer,
			                       .outBufferInfo = m_framebufferInfo };
		m_gBufferPass.AttachToFrameGraph(fg, blackboard, args, camera, m_drawable);
		fg.compile();
		fg.execute();

		if (!m_isHeadless)
			m_swapChain->Present(1, 0);
	}

	IGraphics*
	IGraphics::Create(const GfxOptions& opts)
	{
		return new Graphics(opts);
	}
}
