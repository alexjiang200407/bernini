#include "KernelCode.h"
#include <bgpu/GpuContext.h>
#include <bgpu/d3d12/D3d12ErrorChecker.h>
#include <bgpu/d3d12/native_device.h>
#include <core/err/util.h>
#include <core/math.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <crowdlib/HashFillJob.h>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include <directx/d3d12.h>
#include <wrl/client.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace wrl = Microsoft::WRL;

namespace crowd
{
	namespace
	{
		struct HashFillParams
		{
			uint32_t count;
			uint32_t seed;
		};

		using bgpu::d3d12ErrChecker;

		// The kernel's two parameters as root parameters, at the registers Slang reflected: the
		// params as root constants, the output as a root UAV. No descriptor heap.
		wrl::ComPtr<ID3D12RootSignature>
		CreateRootSignature(ID3D12Device* device, const KernelCode& kernel)
		{
			D3D12_ROOT_PARAMETER parameters[2] = {};

			parameters[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			parameters[0].Constants.ShaderRegister = kernel.params.index;
			parameters[0].Constants.RegisterSpace  = kernel.params.space;
			parameters[0].Constants.Num32BitValues = sizeof(HashFillParams) / sizeof(uint32_t);
			parameters[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

			parameters[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
			parameters[1].Descriptor.ShaderRegister = kernel.output.index;
			parameters[1].Descriptor.RegisterSpace  = kernel.output.space;
			parameters[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

			D3D12_ROOT_SIGNATURE_DESC desc = {};
			desc.NumParameters             = 2;
			desc.pParameters               = parameters;

			wrl::ComPtr<ID3DBlob> serialized;
			wrl::ComPtr<ID3DBlob> error;
			const HRESULT         hr = D3D12SerializeRootSignature(
				&desc,
				D3D_ROOT_SIGNATURE_VERSION_1,
				&serialized,
				&error);
			if (FAILED(hr))
			{
				core::throw_runtime_error(
					"crowd.CSHashFill root signature: {}",
					error ? std::string(
								static_cast<const char*>(error->GetBufferPointer()),
								error->GetBufferSize()) :
							std::string("no message"));
			}

			wrl::ComPtr<ID3D12RootSignature> rootSignature;
			device->CreateRootSignature(
				0,
				serialized->GetBufferPointer(),
				serialized->GetBufferSize(),
				IID_PPV_ARGS(&rootSignature)) >>
				d3d12ErrChecker;
			return rootSignature;
		}

		wrl::ComPtr<ID3D12Resource>
		CreateBuffer(
			ID3D12Device*         device,
			D3D12_HEAP_TYPE       heapType,
			uint64_t              bytes,
			D3D12_RESOURCE_FLAGS  flags,
			D3D12_RESOURCE_STATES initialState,
			const wchar_t*        name)
		{
			D3D12_HEAP_PROPERTIES heap = {};
			heap.Type                  = heapType;

			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width               = bytes;
			desc.Height              = 1;
			desc.DepthOrArraySize    = 1;
			desc.MipLevels           = 1;
			desc.Format              = DXGI_FORMAT_UNKNOWN;
			desc.SampleDesc.Count    = 1;
			desc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags               = flags;

			wrl::ComPtr<ID3D12Resource> buffer;
			device->CreateCommittedResource(
				&heap,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				initialState,
				nullptr,
				IID_PPV_ARGS(&buffer)) >>
				d3d12ErrChecker;
			buffer->SetName(name);
			return buffer;
		}

		D3D12_RESOURCE_BARRIER
		Transition(
			ID3D12Resource*       resource,
			D3D12_RESOURCE_STATES before,
			D3D12_RESOURCE_STATES after) noexcept
		{
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource   = resource;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = before;
			barrier.Transition.StateAfter  = after;
			return barrier;
		}

		// A queue of D3D12_COMMAND_LIST_TYPE_COMPUTE, its allocator, list and fence: the async
		// queue, which the GPU may run beside every direct queue on the device.
		class Job final : public core::RefCounter<HashFillJob>
		{
		public:
			Job(const Job&) = delete;
			Job(Job&&)      = delete;
			Job&
			operator=(const Job&) = delete;
			Job&
			operator=(Job&&) = delete;

			Job(bgpu::GpuContextRef context, const HashFillDesc& desc) :
				m_Context(std::move(context)), m_Count(desc.count)
			{
				if (m_Count == 0)
					core::throw_runtime_error("A hash fill needs at least one element");

				const KernelCode kernel = LoadKernel(*m_Context, c_HashFillModule);
				m_ThreadsPerGroup       = kernel.threadsPerGroup;

				ID3D12Device* device = bgpu::GetD3d12Device(*m_Context);

				D3D12_COMMAND_QUEUE_DESC queueDesc = {};
				queueDesc.Type                     = D3D12_COMMAND_LIST_TYPE_COMPUTE;
				device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_Queue)) >> d3d12ErrChecker;
				m_Queue->SetName(L"crowd compute");

				device->CreateCommandAllocator(
					D3D12_COMMAND_LIST_TYPE_COMPUTE,
					IID_PPV_ARGS(&m_Allocator)) >>
					d3d12ErrChecker;
				device->CreateCommandList(
					0,
					D3D12_COMMAND_LIST_TYPE_COMPUTE,
					m_Allocator.Get(),
					nullptr,
					IID_PPV_ARGS(&m_List)) >>
					d3d12ErrChecker;
				m_List->Close() >> d3d12ErrChecker;

				device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)) >>
					d3d12ErrChecker;
				m_FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				if (m_FenceEvent == nullptr)
					core::throw_runtime_error("crowd compute: CreateEvent failed");

				m_RootSignature = CreateRootSignature(device, kernel);

				D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {};
				pipelineDesc.pRootSignature                    = m_RootSignature.Get();
				pipelineDesc.CS.pShaderBytecode                = kernel.code.data();
				pipelineDesc.CS.BytecodeLength                 = kernel.code.size();
				device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&m_Pipeline)) >>
					d3d12ErrChecker;

				const uint64_t bytes = uint64_t{ m_Count } * sizeof(uint32_t);
				m_Output             = CreateBuffer(
					device,
					D3D12_HEAP_TYPE_DEFAULT,
					bytes,
					D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
					D3D12_RESOURCE_STATE_COMMON,
					L"crowd hash fill");
				m_Readback = CreateBuffer(
					device,
					D3D12_HEAP_TYPE_READBACK,
					bytes,
					D3D12_RESOURCE_FLAG_NONE,
					D3D12_RESOURCE_STATE_COPY_DEST,
					L"crowd hash fill readback");

				// Persistently mapped: a readback heap may stay mapped while the GPU writes it, and
				// GetReadback's precondition is what keeps the CPU from reading mid-write.
				void* mapped = nullptr;
				m_Readback->Map(0, nullptr, &mapped) >> d3d12ErrChecker;
				m_Mapped = static_cast<const uint32_t*>(mapped);
			}

			~Job() noexcept override
			{
				Job::Wait();
				m_Readback->Unmap(0, nullptr);
				CloseHandle(m_FenceEvent);
			}

			uint32_t
			GetCount() const noexcept override
			{
				return m_Count;
			}

			uint64_t
			Submit(uint32_t seed) override
			{
				if (InFlight())
					core::throw_runtime_error("A hash fill is already in flight");

				// The allocator's last list has completed -- that is the precondition -- so it may
				// be reset.
				m_Allocator->Reset() >> d3d12ErrChecker;
				m_List->Reset(m_Allocator.Get(), m_Pipeline.Get()) >> d3d12ErrChecker;

				const HashFillParams params{ .count = m_Count, .seed = seed };

				// A buffer decays to COMMON when the queue's previous list completed.
				const D3D12_RESOURCE_BARRIER toUav = Transition(
					m_Output.Get(),
					D3D12_RESOURCE_STATE_COMMON,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				m_List->ResourceBarrier(1, &toUav);

				m_List->SetComputeRootSignature(m_RootSignature.Get());
				m_List->SetComputeRoot32BitConstants(
					0,
					sizeof(params) / sizeof(uint32_t),
					&params,
					0);
				m_List->SetComputeRootUnorderedAccessView(1, m_Output->GetGPUVirtualAddress());
				m_List->Dispatch(core::div_ceil(m_Count, m_ThreadsPerGroup), 1, 1);

				const D3D12_RESOURCE_BARRIER toCopy = Transition(
					m_Output.Get(),
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
					D3D12_RESOURCE_STATE_COPY_SOURCE);
				m_List->ResourceBarrier(1, &toCopy);
				m_List->CopyBufferRegion(
					m_Readback.Get(),
					0,
					m_Output.Get(),
					0,
					uint64_t{ m_Count } * sizeof(uint32_t));

				const D3D12_RESOURCE_BARRIER toCommon = Transition(
					m_Output.Get(),
					D3D12_RESOURCE_STATE_COPY_SOURCE,
					D3D12_RESOURCE_STATE_COMMON);
				m_List->ResourceBarrier(1, &toCommon);

				m_List->Close() >> d3d12ErrChecker;

				ID3D12CommandList* lists[] = { m_List.Get() };
				m_Queue->ExecuteCommandLists(1, lists);

				++m_Submitted;
				m_Queue->Signal(m_Fence.Get(), m_Submitted) >> d3d12ErrChecker;
				return m_Submitted;
			}

			uint64_t
			GetSubmittedFence() const noexcept override
			{
				return m_Submitted;
			}

			uint64_t
			GetCompletedFence() const noexcept override
			{
				return m_Fence->GetCompletedValue();
			}

			void
			Wait() noexcept override
			{
				// The event only wakes the loop: completion is decided by the fence, so a failed
				// registration costs a slice of polling rather than an unwoken wait.
				if (m_Fence->GetCompletedValue() >= m_Submitted)
					return;

				m_Fence->SetEventOnCompletion(m_Submitted, m_FenceEvent) >> d3d12ErrChecker;
				while (m_Fence->GetCompletedValue() < m_Submitted)
					WaitForSingleObjectEx(m_FenceEvent, 1000, FALSE);
			}

			std::span<const uint32_t>
			GetReadback() const override
			{
				if (m_Submitted == 0 || InFlight())
					core::throw_runtime_error("The hash fill has no finished result");
				return { m_Mapped, m_Count };
			}

		private:
			bgpu::GpuContextRef m_Context;
			uint32_t            m_Count           = 0;
			uint32_t            m_ThreadsPerGroup = 1;
			uint64_t            m_Submitted       = 0;
			const uint32_t*     m_Mapped          = nullptr;
			HANDLE              m_FenceEvent      = nullptr;

			wrl::ComPtr<ID3D12CommandQueue>        m_Queue;
			wrl::ComPtr<ID3D12CommandAllocator>    m_Allocator;
			wrl::ComPtr<ID3D12GraphicsCommandList> m_List;
			wrl::ComPtr<ID3D12Fence>               m_Fence;
			wrl::ComPtr<ID3D12RootSignature>       m_RootSignature;
			wrl::ComPtr<ID3D12PipelineState>       m_Pipeline;
			wrl::ComPtr<ID3D12Resource>            m_Output;
			wrl::ComPtr<ID3D12Resource>            m_Readback;
		};
	}

	HashFillJobRef
	CreateHashFillJob(bgpu::GpuContextRef context, const HashFillDesc& desc)
	{
		if (context == nullptr)
			core::throw_runtime_error("A hash fill needs a GPU context");
		return core::SharedRef<Job>::Make(std::move(context), desc);
	}
}
