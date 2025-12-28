#include "camera/Camera.h"
#include "drawable/Drawable.h"
#include "ffi/util.h"
#include "graphics/Graphics.h"
#include "material/Material.h"
#include "mesh/MeshFactory.h"
#include "passes/GBufferPass.h"
#include <dxgidebug.h>
#include <fg/Blackboard.hpp>
#include <nvrhi/validation.h>

namespace gfx
{
	constexpr auto bufferCount = 2u;

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

		nvrhi::DeviceHandle
		GetDevice() noexcept override
		{
			return m_nvrhiDevice;
		}

	private:
		nvrhi::DeviceHandle               m_nvrhiDevice;
		nvrhi::TextureHandle              m_nvrhiDepthBuffer;
		std::vector<nvrhi::TextureHandle> m_nvrhiBackBuffers;
		nvrhi::FramebufferInfo            m_framebufferInfo;

		nvrhi::RefCountPtr<ID3D12Device>       m_device;
		nvrhi::RefCountPtr<ID3D12CommandQueue> m_commandQueue;
		nvrhi::RefCountPtr<IDXGISwapChain3>    m_swapChain;

		nvrhi::RefCountPtr<ID3D12Resource>              m_depthBuffer;
		std::vector<nvrhi::RefCountPtr<ID3D12Resource>> m_backBuffers;

		std::unique_ptr<MeshFactory>           m_meshFactory;
		GBufferPass                            m_gBufferPass;
		std::vector<std::unique_ptr<Drawable>> m_drawable;

		std::vector<HANDLE>             m_frameFenceEvents;
		nvrhi::RefCountPtr<ID3D12Fence> m_frameFence;

		nvrhi::RefCountPtr<ID3D12Debug1>    m_debugController;
		nvrhi::RefCountPtr<IDXGIInfoQueue>  m_dxgiInfoQueue;
		nvrhi::RefCountPtr<ID3D12InfoQueue> m_infoQueue;

		ErrorCallback m_errorCB;

		bool   m_isHeadless               = false;
		UINT   m_windowWidth              = 0;
		UINT   m_windowHeight             = 0;
		UINT64 m_frameCount               = 1;
		bool   m_enableGPUValidationLayer = false;
		bool   m_enableDebugLayer         = false;

		nvrhi::CommandListHandle m_mainCommandList;
	};

	Graphics::Graphics(const GfxOptions& opts)
	{
		m_isHeadless               = opts.headless;
		m_windowWidth              = opts.width;
		m_windowHeight             = opts.height;
		m_enableDebugLayer         = opts.enableDebugLayer;
		m_enableGPUValidationLayer = opts.enableDebugLayer && opts.enableGPUValidationLayer;

		if (opts.enablePixDebug)
		{
			LoadLibraryA("WinPixGpuCapturer.dll");
		}

		if (m_enableDebugLayer)
		{
			D3D12GetDebugInterface(IID_PPV_ARGS(&m_debugController)) >> dx::dxErrorChecker;
			m_debugController->EnableDebugLayer();
			if (m_enableGPUValidationLayer)
				m_debugController->SetEnableGPUBasedValidation(TRUE);
		}

		CreateDevice();

		m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frameFence)) >>
			dx::dxErrorChecker;

		for (UINT i = 0; i < bufferCount; i++)
			m_frameFenceEvents.push_back(CreateEvent(nullptr, false, true, nullptr));

		if (m_enableDebugLayer)
		{
			DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_dxgiInfoQueue)) >> dx::dxErrorChecker;

			m_dxgiInfoQueue->SetBreakOnSeverity(
				DXGI_DEBUG_ALL,
				DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,
				TRUE);

			m_dxgiInfoQueue->SetBreakOnSeverity(
				DXGI_DEBUG_ALL,
				DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,
				TRUE);
		}

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

		m_gBufferPass.Init(m_nvrhiDevice);

		if (m_enableDebugLayer)
		{
			m_nvrhiDevice = nvrhi::validation::createValidationLayer(m_nvrhiDevice);
		}

		if (!m_isHeadless)
			CreateRenderTargets();

		m_meshFactory     = std::make_unique<MeshFactory>(m_nvrhiDevice);
		m_mainCommandList = m_nvrhiDevice->createCommandList();

		auto mat      = glm::mat4{ 1.0f };
		mat[3][0]     = 5.0f;
		auto drawable = std::make_unique<Drawable>(static_cast<ShaderMatrix>(mat));
		drawable->SetMesh(m_meshFactory->CreateSphere("shaders/VS_cube.cso"sv));
		drawable->SetMaterial(std::make_shared<Material>(m_nvrhiDevice, "shaders/PS_cube.cso"sv));
		m_drawable.push_back(std::move(drawable));

		GeomPass::Setup(m_nvrhiDevice);
	}

	Graphics::~Graphics()
	{
		ReleaseRenderTargets();

		for (auto fenceEvent : m_frameFenceEvents)
		{
			WaitForSingleObject(fenceEvent, INFINITE);
		}

		m_nvrhiDevice.Reset();

		for (auto fenceEvent : m_frameFenceEvents)
		{
			CloseHandle(fenceEvent);
		}
		m_frameFenceEvents.clear();

		if (m_swapChain)
		{
			m_swapChain->SetFullscreenState(false, nullptr);
		}

		GeomPass::Shutdown();

		if (m_infoQueue)
			m_infoQueue->ClearStoredMessages();

		if (m_dxgiInfoQueue)
			m_dxgiInfoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);
	}

	void
	Graphics::CreateDevice()
	{
		D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)) >>
			gfx::dx::dxErrorChecker;

		auto cqDesc  = D3D12_COMMAND_QUEUE_DESC{};
		cqDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
		cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		m_device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&m_commandQueue)) >>
			gfx::dx::dxErrorChecker;

		if (m_enableDebugLayer)
		{
			m_device->QueryInterface(IID_PPV_ARGS(&m_infoQueue));
			if (m_infoQueue)
				m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		}
	}

	void
	Graphics::CreateSwapChain(HWND hwnd)
	{
		auto sd        = DXGI_SWAP_CHAIN_DESC1{};
		sd.Width       = m_windowWidth;
		sd.Height      = m_windowHeight;
		sd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.BufferCount = bufferCount;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.Scaling     = DXGI_SCALING_STRETCH;
		sd.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;

		sd.SampleDesc.Count   = 1;
		sd.SampleDesc.Quality = 0;

		auto factory = nvrhi::RefCountPtr<IDXGIFactory4>{};

		if (m_enableDebugLayer)
		{
			CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)) >>
				gfx::dx::dxErrorChecker;
		}
		else
		{
			CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)) >> gfx::dx::dxErrorChecker;
		}

		auto swap = nvrhi::RefCountPtr<IDXGISwapChain1>{};
		factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &sd, nullptr, nullptr, &swap) >>
			gfx::dx::dxErrorChecker;

		swap->QueryInterface(IID_PPV_ARGS(&m_swapChain));

		factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER) >> gfx::dx::dxErrorChecker;
	}

	void
	Graphics::CreateDepthBuffer()
	{
		auto desc             = D3D12_RESOURCE_DESC{};
		desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width            = m_windowWidth;
		desc.Height           = m_windowHeight;
		desc.DepthOrArraySize = 1;
		desc.MipLevels        = 1;
		desc.Format           = DXGI_FORMAT_D24_UNORM_S8_UINT;
		desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.SampleDesc.Count = 1;

		auto heapProps = D3D12_HEAP_PROPERTIES{};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		auto clear               = D3D12_CLEAR_VALUE{};
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
		m_backBuffers.resize(bufferCount);
		m_nvrhiBackBuffers.resize(bufferCount);

		auto texDesc = nvrhi::TextureDesc{};
		texDesc.setIsRenderTarget(true)
			.setFormat(nvrhi::Format::BGRA8_UNORM)
			.setSampleCount(1)
			.setWidth(m_windowWidth)
			.setHeight(m_windowHeight)
			.setInitialState(nvrhi::ResourceStates::Present)
			.setKeepInitialState(true)
			.setDebugName("BackBuffer");

		for (UINT i = 0; i < bufferCount; i++)
		{
			m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
			m_nvrhiBackBuffers[i] = m_nvrhiDevice->createHandleForNativeTexture(
				nvrhi::ObjectTypes::D3D12_Resource,
				m_backBuffers[i].Get(),
				texDesc);
		}

		// TODO: Move this to gbuffer pass
		auto depthDesc = nvrhi::TextureDesc{};
		depthDesc.setWidth(m_windowWidth)
			.setHeight(m_windowHeight)
			.setFormat(nvrhi::Format::D24S8)
			.setIsRenderTarget(true)
			.setInitialState(nvrhi::ResourceStates::DepthWrite)
			.setKeepInitialState(true)
			.setIsTypeless(true)
			.setDebugName("DepthBuffer");

		m_nvrhiDepthBuffer = m_nvrhiDevice->createHandleForNativeTexture(
			nvrhi::ObjectTypes::D3D12_Resource,
			m_depthBuffer.Get(),
			depthDesc);

		m_framebufferInfo = nvrhi::FramebufferInfo{}
		                        .addColorFormat(nvrhi::Format::BGRA8_UNORM)
		                        .setDepthFormat(nvrhi::Format::D24S8);
	}

	void
	Graphics::ReleaseRenderTargets()
	{
		if (m_nvrhiDevice)
		{
			auto success = m_nvrhiDevice->waitForIdle();
			if (!success)
			{
				logger::error("Failed to wait for device idle during Graphics shutdown");
			}
			m_nvrhiDevice->runGarbageCollection();
		}

		for (auto e : m_frameFenceEvents) SetEvent(e);

		m_nvrhiDepthBuffer.Reset();
		m_nvrhiBackBuffers.clear();

		m_backBuffers.clear();
		m_depthBuffer.Reset();
	}

	void
	Graphics::DrawFrame(Camera& camera)
	{
		auto backBufferIndex  = m_swapChain->GetCurrentBackBufferIndex();
		auto nvrhiFramebuffer = m_nvrhiDevice->createFramebuffer(
			nvrhi::FramebufferDesc{}
				.addColorAttachment(m_nvrhiBackBuffers[backBufferIndex])
				.setDepthAttachment(m_nvrhiDepthBuffer));

		WaitForSingleObject(m_frameFenceEvents[backBufferIndex], INFINITE);

		m_mainCommandList->open();

		nvrhi::utils::ClearColorAttachment(
			m_mainCommandList,
			nvrhiFramebuffer,
			0,
			nvrhi::Color{ 0, 0, 0, 1 });

		nvrhi::utils::ClearDepthStencilAttachment(m_mainCommandList, nvrhiFramebuffer, 1.0f, 0);

		m_mainCommandList->close();

		m_nvrhiDevice->executeCommandList(m_mainCommandList);

		FrameGraph           fg;
		FrameGraphBlackboard blackboard;
		RenderArgs           args{ .screenWidth   = static_cast<float>(m_windowWidth),
			                       .screenHeight  = static_cast<float>(m_windowHeight),
			                       .device        = m_nvrhiDevice,
			                       .outBuffer     = nvrhiFramebuffer,
			                       .outBufferInfo = m_framebufferInfo };
		m_gBufferPass.AttachToFrameGraph(fg, blackboard, args, camera, m_drawable);

		fg.compile();
		fg.execute();

		if (!m_isHeadless)
			m_swapChain->Present(1, 0);

		m_commandQueue->Signal(m_frameFence.Get(), m_frameCount);
		m_frameFence->SetEventOnCompletion(m_frameCount, m_frameFenceEvents[backBufferIndex]);

		//std::this_thread::sleep_for(std::chrono::milliseconds(0));
		//m_nvrhiDevice->runGarbageCollection();

		++m_frameCount;
	}

	IGraphics*
	IGraphics::Create(const GfxOptions& opts)
	{
		return new Graphics(opts);
	}
}
