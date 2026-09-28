#include "ContextBase.h"
#include <bgpu/GpuContext.h>
#include <bgpu/d3d12/D3d12ErrorChecker.h>
#include <bgpu/d3d12/native_device.h>
#include <core/err/util.h>
#include <core/log/log.h>
#include <core/ref/SharedRef.h>
#include <atomic>
#include <slang.h>
#include <spdlog/spdlog.h>

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl/client.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace wrl = Microsoft::WRL;

namespace bgpu
{
	namespace
	{
		// Never cleared. SetEnableGPUBasedValidation sets it on the debug layer, which is the
		// process's and not this controller's: every device created afterwards is instrumented,
		// including one whose desc did not ask and one created after this context is gone. A
		// successor that reports "off" while running instrumented would have its owners cache
		// driver pipelines built without the instrumentation.
		std::atomic<bool> g_GpuValidationActive = false;

		class Context final : public ContextBase
		{
		public:
			Context(const Context&) = delete;
			Context(Context&&)      = delete;
			Context&
			operator=(const Context&) = delete;
			Context&
			operator=(Context&&) = delete;

			explicit Context(const GpuContextDesc& desc) : ContextBase(desc, SLANG_DXIL)
			{
				core::logging::init_file_logger("bgpu.log", static_cast<int>(desc.logLevel));

				if (desc.enablePixDebug)
					LoadLibraryA("WinPixGpuCapturer.dll");

				if (desc.enableDebugLayer)
				{
					D3D12GetDebugInterface(IID_PPV_ARGS(&m_DebugController)) >> d3d12ErrChecker;
					m_DebugController->EnableDebugLayer();
					if (desc.enableGPUValidationLayer)
					{
						m_DebugController->SetEnableGPUBasedValidation(TRUE);
						g_GpuValidationActive.store(true, std::memory_order_relaxed);
					}

					DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_DxgiInfoQueue)) >> d3d12ErrChecker;
					m_DxgiInfoQueue->SetBreakOnSeverity(
						DXGI_DEBUG_ALL,
						DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,
						TRUE);
					m_DxgiInfoQueue->SetBreakOnSeverity(
						DXGI_DEBUG_ALL,
						DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,
						TRUE);
				}

				const HRESULT created =
					D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_Device));
				if (FAILED(created))
				{
					core::throw_runtime_error(
						"no D3D12 device at feature level 12_0 (0x{:08X})",
						static_cast<uint32_t>(created));
				}

				// Debug-layer and GPU-based-validation messages otherwise only reach an attached
				// debugger.
				if (desc.enableDebugLayer && SUCCEEDED(m_Device.As(&m_InfoQueue)))
				{
					m_InfoQueue->RegisterMessageCallback(
						&Context::LogMessage,
						D3D12_MESSAGE_CALLBACK_FLAG_NONE,
						this,
						&m_MessageCallbackCookie) >>
						d3d12ErrChecker;
				}
			}

			~Context() noexcept override
			{
				spdlog::trace("~GpuContext");

				// Everything an owner made on the device is gone by the time its context reference
				// drops, so whatever the report names is a leak, attributed to whoever made it.
				m_Device.Reset();
				m_DxgiInfoQueue.Reset();
				m_DebugController.Reset();

				if (m_InfoQueue && m_MessageCallbackCookie != 0)
					m_InfoQueue->UnregisterMessageCallback(m_MessageCallbackCookie);
				m_InfoQueue.Reset();

				if (GetDesc().enableDebugLayer)
				{
					wrl::ComPtr<IDXGIDebug1> dxgiDebug;
					if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
						dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
				}
			}

			bool
			GpuValidationActive() const noexcept override
			{
				return g_GpuValidationActive.load(std::memory_order_relaxed);
			}

			[[nodiscard]] ID3D12Device*
			GetDevice() const noexcept
			{
				return m_Device.Get();
			}

		private:
			static void CALLBACK
			LogMessage(
				D3D12_MESSAGE_CATEGORY /*category*/,
				D3D12_MESSAGE_SEVERITY severity,
				D3D12_MESSAGE_ID /*id*/,
				LPCSTR description,
				void*  context)
			{
				bool severe = false;
				switch (severity)
				{
				case D3D12_MESSAGE_SEVERITY_CORRUPTION:
				case D3D12_MESSAGE_SEVERITY_ERROR:
					spdlog::error("[D3D12] {}", description);
					severe = true;
					break;
				case D3D12_MESSAGE_SEVERITY_WARNING:
					spdlog::warn("[D3D12] {}", description);
					severe = true;
					break;
				case D3D12_MESSAGE_SEVERITY_INFO:
					spdlog::info("[D3D12] {}", description);
					break;
				case D3D12_MESSAGE_SEVERITY_MESSAGE:
				default:
					spdlog::debug("[D3D12] {}", description);
					break;
				}

				const auto* self = static_cast<const Context*>(context);
				if (severe && self != nullptr && self->GetDesc().strictError)
					core::fatal("[D3D12] strict error: {}", description);
			}

			wrl::ComPtr<ID3D12Device>     m_Device;
			wrl::ComPtr<ID3D12Debug1>     m_DebugController;
			wrl::ComPtr<IDXGIInfoQueue>   m_DxgiInfoQueue;
			wrl::ComPtr<ID3D12InfoQueue1> m_InfoQueue;
			DWORD                         m_MessageCallbackCookie = 0;
		};
	}

	ID3D12Device*
	GetD3d12Device(const GpuContext& context) noexcept
	{
		const auto* d3d12 = dynamic_cast<const Context*>(&context);
		core::ensure(d3d12 != nullptr, "The GPU context is not a D3D12 one");
		return d3d12->GetDevice();
	}

	GpuContextRef
	CreateGpuContext(const GpuContextDesc& desc)
	{
		return core::SharedRef<Context>::Make(desc);
	}
}
