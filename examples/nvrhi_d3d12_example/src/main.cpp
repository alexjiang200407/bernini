#include <core/win/Window.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgidebug.h>
#include <nvrhi/d3d12.h>
#include <nvrhi/utils.h>
#include <nvrhi/validation.h>

constexpr auto BUFFER_COUNT = 2u;

class ErrorCallback : public nvrhi::IMessageCallback
{
	void
	message(nvrhi::MessageSeverity severity, const char* messageText) override
	{
		switch (severity)
		{
		case nvrhi::MessageSeverity::Info:
			printf("NVRHI Info: %s\n", messageText);
			break;
		case nvrhi::MessageSeverity::Warning:
			printf("NVRHI Warning: %s\n", messageText);
			break;
		case nvrhi::MessageSeverity::Error:
			printf("NVRHI Error: %s\n", messageText);
			break;
		case nvrhi::MessageSeverity::Fatal:
			printf("NVRHI Fatal Error: %s\n", messageText);
			break;
		}
	}
};

nvrhi::DeviceHandle                    nvrhiDevice;
nvrhi::RefCountPtr<ID3D12Device>       d3d12Device;
nvrhi::RefCountPtr<ID3D12CommandQueue> commandQueue;
nvrhi::RefCountPtr<IDXGISwapChain3>    swapChain;
std::vector<nvrhi::TextureHandle>      backBuffers;
ErrorCallback                          errorCallback;
nvrhi::RefCountPtr<ID3D12Debug1>       debugController;
nvrhi::RefCountPtr<IDXGIInfoQueue>     dxgiInfoQueue;
nvrhi::RefCountPtr<ID3D12InfoQueue>    d3d12InfoQueue;
nvrhi::CommandListHandle               mainCommandList;

// Frame synchronization
nvrhi::RefCountPtr<ID3D12Fence> frameFence;
std::vector<HANDLE>             frameFenceEvents;
UINT64                          frameIdx = 1;

UINT windowWidth  = 1920;
UINT windowHeight = 1080;

namespace
{
	void
	InitializeD3D12Device()
	{
		D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d12Device));

		D3D12_COMMAND_QUEUE_DESC cqDesc = {};
		cqDesc.Type                     = D3D12_COMMAND_LIST_TYPE_DIRECT;
		cqDesc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;
		d3d12Device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue));

		d3d12Device->QueryInterface(IID_PPV_ARGS(&d3d12InfoQueue));
		d3d12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);

		d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frameFence));

		for (UINT i = 0; i < BUFFER_COUNT; i++)
		{
			frameFenceEvents.push_back(CreateEvent(nullptr, false, true, nullptr));
		}
	}

	void
	EnableDebugLayers()
	{
		D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}

	void
	SetupDXGIDebugLayer()
	{
		DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue));
		dxgiInfoQueue->SetBreakOnSeverity(
			DXGI_DEBUG_ALL,
			DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,
			TRUE);

		dxgiInfoQueue->SetBreakOnSeverity(
			DXGI_DEBUG_ALL,
			DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,
			TRUE);
	}

	void
	CreateSwapChain(HWND hwnd)
	{
		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.Width                 = windowWidth;
		sd.Height                = windowHeight;
		sd.Format                = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.BufferCount           = BUFFER_COUNT;
		sd.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.Scaling               = DXGI_SCALING_STRETCH;
		sd.AlphaMode             = DXGI_ALPHA_MODE_IGNORE;
		sd.SampleDesc.Count      = 1;
		sd.SampleDesc.Quality    = 0;

		nvrhi::RefCountPtr<IDXGIFactory4> factory;
		CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory));

		nvrhi::RefCountPtr<IDXGISwapChain1> swap;
		factory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &sd, nullptr, nullptr, &swap);
		swap->QueryInterface(IID_PPV_ARGS(&swapChain));

		factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
	}

	void
	InitializeNVRHIDevice()
	{
		nvrhi::d3d12::DeviceDesc deviceDesc = {};
		deviceDesc.pDevice                  = d3d12Device.Get();
		deviceDesc.pGraphicsCommandQueue    = commandQueue.Get();
		deviceDesc.errorCB                  = &errorCallback;

		nvrhiDevice     = nvrhi::d3d12::createDevice(deviceDesc);
		mainCommandList = nvrhiDevice->createCommandList();
	}

	void
	CreateRenderTargets()
	{
		backBuffers.resize(BUFFER_COUNT);

		nvrhi::TextureDesc texDesc = {};
		texDesc.setIsRenderTarget(true)
			.setFormat(nvrhi::Format::BGRA8_UNORM)
			.setSampleCount(1)
			.setWidth(windowWidth)
			.setHeight(windowHeight)
			.setInitialState(nvrhi::ResourceStates::Present)
			.setKeepInitialState(true)
			.setDebugName("BackBuffer");

		for (UINT i = 0; i < BUFFER_COUNT; i++)
		{
			nvrhi::RefCountPtr<ID3D12Resource> backBuffer;
			swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

			backBuffers[i] = nvrhiDevice->createHandleForNativeTexture(
				nvrhi::ObjectTypes::D3D12_Resource,
				backBuffer.Get(),
				texDesc);
		}
	}

	void
	PresentFrame()
	{
		UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

		WaitForSingleObject(frameFenceEvents[backBufferIndex], INFINITE);

		mainCommandList->open();

		nvrhi::FramebufferHandle framebuffer = nvrhiDevice->createFramebuffer(
			nvrhi::FramebufferDesc{}.addColorAttachment(backBuffers[backBufferIndex]));

		float speed = 0.05f;
		float wave  = std::sin(static_cast<float>(frameIdx) * speed) * 0.5f + 0.5f;

		nvrhi::Color clearColor(0.0f, wave * 0.5f, wave, 1.0f);

		nvrhi::utils::ClearColorAttachment(mainCommandList, framebuffer, 0, clearColor);

		mainCommandList->close();
		nvrhiDevice->executeCommandList(mainCommandList);

		swapChain->Present(1, 0);

		commandQueue->Signal(frameFence.Get(), frameIdx);

		frameFence->SetEventOnCompletion(frameIdx, frameFenceEvents[backBufferIndex]);

		nvrhiDevice->runGarbageCollection();

		++frameIdx;
	}

	void
	Cleanup()
	{
		for (auto fenceEvent : frameFenceEvents)
		{
			WaitForSingleObject(fenceEvent, INFINITE);
		}

		nvrhiDevice->waitForIdle();
		nvrhiDevice->runGarbageCollection();

		for (auto fenceEvent : frameFenceEvents)
		{
			CloseHandle(fenceEvent);
		}
		frameFenceEvents.clear();

		if (d3d12InfoQueue)
			d3d12InfoQueue->ClearStoredMessages();

		if (dxgiInfoQueue)
			dxgiInfoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);

		if (swapChain)
			swapChain->SetFullscreenState(false, nullptr);
	}
}

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	auto opts = core::win::WindowOptions{};

	opts.width     = 800;
	opts.height    = 600;
	opts.resizable = false;
	opts.decorated = false;
	opts.mode      = core::win::WindowOptions::Mode::BorderlessWindowed;

	auto wnd = core::win::IWindow::Create(opts);

	LoadLibraryA("WinPixGpuCapturer.dll");

	EnableDebugLayers();
	InitializeD3D12Device();
	SetupDXGIDebugLayer();
	CreateSwapChain(GetActiveWindow());
	InitializeNVRHIDevice();
	CreateRenderTargets();

	for (auto res = wnd->Process(); res != core::win::IWindow::kClose; res = wnd->Process())
	{
		PresentFrame();
	}

	Cleanup();

	return 0;
}
