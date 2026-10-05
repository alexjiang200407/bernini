#include "ContextBase.h"
#include "native_device_d3d12.h"
#include <atomic>
#include <bgpu/GpuContext.h>
#include <bgpu/SystemRequirements.h>
#include <bgpu/d3d12/D3d12ErrorChecker.h>
#include <core/err/util.h>
#include <core/log/log.h>
#include <core/ref/SharedRef.h>
#include <core/str/str.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <slang.h>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>
#include <vector>

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

		// D3D12CreateDevice with no adapter takes DXGI's first, so that is the GPU a player is told of.
		[[nodiscard]] std::string
		DefaultAdapterName()
		{
			wrl::ComPtr<IDXGIFactory1> factory;
			wrl::ComPtr<IDXGIAdapter1> adapter;
			auto                       desc = DXGI_ADAPTER_DESC1();
			if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
			    FAILED(factory->EnumAdapters1(0, &adapter)) || FAILED(adapter->GetDesc1(&desc)))
				return {};
			return core::str::wide_to_string(desc.Description);
		}

		// A query the runtime does not know leaves its feature at zero, which reads as unsupported.
		[[nodiscard]] D3d12SystemFacts
		ReadSystemFacts(ID3D12Device* device)
		{
			auto facts    = D3d12SystemFacts();
			facts.gpuName = DefaultAdapterName();
			if (device == nullptr)
				return facts;
			facts.device = true;

			auto shaderModel               = D3D12_FEATURE_DATA_SHADER_MODEL();
			shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_6;
			if (SUCCEEDED(device->CheckFeatureSupport(
					D3D12_FEATURE_SHADER_MODEL,
					&shaderModel,
					sizeof(shaderModel))))
				facts.shaderModel = static_cast<uint32_t>(shaderModel.HighestShaderModel);

			auto options = D3D12_FEATURE_DATA_D3D12_OPTIONS();
			if (SUCCEEDED(device->CheckFeatureSupport(
					D3D12_FEATURE_D3D12_OPTIONS,
					&options,
					sizeof(options))))
				facts.resourceBindingTier = static_cast<uint32_t>(options.ResourceBindingTier);

			auto options7 = D3D12_FEATURE_DATA_D3D12_OPTIONS7();
			if (SUCCEEDED(device->CheckFeatureSupport(
					D3D12_FEATURE_D3D12_OPTIONS7,
					&options7,
					sizeof(options7))))
				facts.meshShaderTier = static_cast<uint32_t>(options7.MeshShaderTier);

			auto options12 = D3D12_FEATURE_DATA_D3D12_OPTIONS12();
			if (SUCCEEDED(device->CheckFeatureSupport(
					D3D12_FEATURE_D3D12_OPTIONS12,
					&options12,
					sizeof(options12))))
				facts.enhancedBarriers = options12.EnhancedBarriersSupported == TRUE;

			return facts;
		}

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
				// Only "unsupported" says the machine is below the bar; anything else is a fault in a
				// driver that could create one, and telling the player to replace the GPU would be wrong.
				if (FAILED(created) && created != DXGI_ERROR_UNSUPPORTED &&
				    created != E_NOINTERFACE)
				{
					core::throw_runtime_error(
						"no D3D12 device at feature level 12_0 (0x{:08X})",
						static_cast<uint32_t>(created));
				}

				// Below the bar the first mesh pipeline is where it would fail, with nothing a player
				// could act on.
				std::vector<UnmetRequirement> unmet =
					CheckSystemRequirements(ReadSystemFacts(m_Device.Get()));
				if (!unmet.empty())
				{
					auto error = UnsupportedSystem(std::move(unmet));
					spdlog::critical("{}", error.what());
					throw error;
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

				// Before the device: the shared pipeline states are the context's own, not a leak.
				m_PipelineStates.clear();

				m_Device.Reset();
				m_DebugController.Reset();

				if (m_InfoQueue && m_MessageCallbackCookie != 0)
					m_InfoQueue->UnregisterMessageCallback(m_MessageCallbackCookie);
				m_InfoQueue.Reset();

				if (m_DxgiInfoQueue)
				{
					LogLiveObjects();
					m_DxgiInfoQueue.Reset();
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

			[[nodiscard]] ID3D12PipelineState*
			FindPipelineState(ID3D12RootSignature* rootSignature, uint64_t identity) const noexcept
			{
				const auto lock = std::scoped_lock(m_PipelineStatesMutex);
				const auto it   = m_PipelineStates.find(PipelineKey{ rootSignature, identity });
				if (it == m_PipelineStates.end())
					return nullptr;

				it->second.pipelineState->AddRef();
				return it->second.pipelineState.Get();
			}

			void
			SharePipelineState(
				ID3D12RootSignature* rootSignature,
				uint64_t             identity,
				ID3D12PipelineState* pipelineState) const noexcept
			{
				const auto lock = std::scoped_lock(m_PipelineStatesMutex);
				m_PipelineStates.try_emplace(
					PipelineKey{ rootSignature, identity },
					SharedPipeline{ rootSignature, pipelineState });
			}

		private:
			// Everything an owner made on the device is gone by the time its context reference
			// drops, so each object reported is a leak, named by its debug name. The report reaches
			// no message callback -- the device's is gone -- so it is read back from the queue it is
			// stored in, or it goes only to an attached debugger.
			void
			LogLiveObjects() const noexcept
			{
				wrl::ComPtr<IDXGIDebug1> dxgiDebug;
				if (FAILED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
					return;

				m_DxgiInfoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);
				dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);

				size_t       live  = 0;
				const UINT64 count = m_DxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
				for (UINT64 i = 0; i < count; ++i)
				{
					SIZE_T size = 0;
					if (FAILED(m_DxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &size)))
						continue;

					auto  bytes   = std::vector<std::byte>(size);
					auto* message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(bytes.data());
					if (FAILED(m_DxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, message, &size)))
						continue;

					// Each live object is a warning; the report's framing lines are not.
					if (message->Severity > DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING)
						continue;

					spdlog::error("[D3D12] outlived the GPU context: {}", message->pDescription);
					++live;
				}
				m_DxgiInfoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);

				if (live > 0 && GetDesc().strictError)
					core::fatal(
						"[D3D12] strict error: {} object(s) outlived the GPU context",
						live);
			}

			// The root signature is part of the key by address, so the entry holds it: a freed one's
			// address could otherwise come back as a different signature.
			using PipelineKey = std::pair<ID3D12RootSignature*, uint64_t>;

			struct SharedPipeline
			{
				wrl::ComPtr<ID3D12RootSignature> rootSignature;
				wrl::ComPtr<ID3D12PipelineState> pipelineState;
			};

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

			mutable std::mutex                            m_PipelineStatesMutex;
			mutable std::map<PipelineKey, SharedPipeline> m_PipelineStates;
		};

		[[nodiscard]] const Context&
		AsD3d12(const GpuContext& context) noexcept
		{
			const auto* d3d12 = dynamic_cast<const Context*>(&context);
			core::ensure(d3d12 != nullptr, "The GPU context is not a D3D12 one");
			return *d3d12;
		}
	}

	ID3D12Device*
	GetD3d12Device(const GpuContext& context) noexcept
	{
		return AsD3d12(context).GetDevice();
	}

	ID3D12PipelineState*
	FindPipelineState(
		const GpuContext&    context,
		ID3D12RootSignature* rootSignature,
		uint64_t             identity) noexcept
	{
		return AsD3d12(context).FindPipelineState(rootSignature, identity);
	}

	void
	SharePipelineState(
		const GpuContext&    context,
		ID3D12RootSignature* rootSignature,
		uint64_t             identity,
		ID3D12PipelineState* pipelineState) noexcept
	{
		AsD3d12(context).SharePipelineState(rootSignature, identity, pipelineState);
	}

	GpuContextRef
	CreateGpuContext(const GpuContextDesc& desc)
	{
		return core::SharedRef<Context>::Make(desc);
	}
}
