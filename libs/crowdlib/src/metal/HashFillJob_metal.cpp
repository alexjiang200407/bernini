#include "KernelCode.h"
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <bgpu/GpuContext.h>
#include <bgpu/metal/native_device.h>
#include <core/err/util.h>
#include <core/math.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <crowdlib/HashFillJob.h>
#include <cstdint>
#include <span>
#include <spdlog/spdlog.h>
#include <utility>

namespace crowd
{
	namespace
	{
		struct HashFillParams
		{
			uint32_t count;
			uint32_t seed;
		};

		const char*
		DescriptionOf(const NS::Error* error) noexcept
		{
			if (error == nullptr || error->localizedDescription() == nullptr)
				return "unknown error";
			return error->localizedDescription()->utf8String();
		}

		// Metal has no compute-typed queue: a second MTLCommandQueue is the async one, and the GPU
		// may run its command buffers concurrently with every other queue's. The shared event is its
		// fence.
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

				const KernelCode kernel = CompileKernel(*m_Context, c_HashFillModule);
				m_ParamsIndex           = kernel.params.index;
				m_OutputIndex           = kernel.output.index;
				m_ThreadsPerGroup       = kernel.threadsPerGroup;

				NS::SharedPtr<NS::AutoreleasePool> pool =
					NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

				MTL::Device* device = bgpu::GetMtlDevice(*m_Context);

				m_Queue = NS::TransferPtr(device->newCommandQueue());
				m_Event = NS::TransferPtr(device->newSharedEvent());
				core::ensure(m_Queue && m_Event, "Metal compute queue creation failed");
				m_Queue->setLabel(NS::String::string("crowd compute", NS::UTF8StringEncoding));

				NS::Error*                  error   = nullptr;
				NS::SharedPtr<MTL::Library> library = NS::TransferPtr(device->newLibrary(
					NS::String::string(kernel.code.c_str(), NS::UTF8StringEncoding),
					nullptr,
					&error));
				if (!library)
					core::throw_runtime_error("crowd.CSHashFill: {}", DescriptionOf(error));

				// Slang mangles the entry name in MSL (main -> main_0), and the library holds only it.
				NS::Array* names = library->functionNames();
				core::ensure(names->count() == 1, "The kernel library must hold one function");
				NS::SharedPtr<MTL::Function> function = NS::TransferPtr(
					library->newFunction(static_cast<NS::String*>(names->object(0))));

				m_Pipeline =
					NS::TransferPtr(device->newComputePipelineState(function.get(), &error));
				if (!m_Pipeline)
					core::throw_runtime_error("crowd.CSHashFill: {}", DescriptionOf(error));

				const NS::UInteger bytes = NS::UInteger{ m_Count } * sizeof(uint32_t);
				m_Output =
					NS::TransferPtr(device->newBuffer(bytes, MTL::ResourceStorageModePrivate));
				m_Readback =
					NS::TransferPtr(device->newBuffer(bytes, MTL::ResourceStorageModeShared));
				core::ensure(m_Output && m_Readback, "Metal buffer creation failed");
				m_Output->setLabel(NS::String::string("crowd hash fill", NS::UTF8StringEncoding));
				m_Readback->setLabel(
					NS::String::string("crowd hash fill readback", NS::UTF8StringEncoding));
			}

			~Job() noexcept override { Job::Wait(); }

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

				// Everything below autoreleases; the pool is released once the buffer is committed,
				// which the driver then owns until it completes.
				NS::SharedPtr<NS::AutoreleasePool> pool =
					NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

				MTL::CommandBuffer* cmdBuffer = m_Queue->commandBuffer();
				core::ensure(cmdBuffer != nullptr, "Metal command buffer creation failed");

				const HashFillParams params{ .count = m_Count, .seed = seed };

				MTL::ComputeCommandEncoder* compute = cmdBuffer->computeCommandEncoder();
				compute->setComputePipelineState(m_Pipeline.get());
				compute->setBytes(&params, sizeof(params), m_ParamsIndex);
				compute->setBuffer(m_Output.get(), 0, m_OutputIndex);
				compute->dispatchThreadgroups(
					MTL::Size(core::div_ceil(m_Count, m_ThreadsPerGroup), 1, 1),
					MTL::Size(m_ThreadsPerGroup, 1, 1));
				compute->endEncoding();

				// Encoders on one command buffer run in order, so the copy sees the kernel's writes.
				MTL::BlitCommandEncoder* blit = cmdBuffer->blitCommandEncoder();
				blit->copyFromBuffer(
					m_Output.get(),
					0,
					m_Readback.get(),
					0,
					NS::UInteger{ m_Count } * sizeof(uint32_t));
				blit->endEncoding();

				++m_Submitted;
				cmdBuffer->encodeSignalEvent(m_Event.get(), m_Submitted);
				cmdBuffer->addCompletedHandler([](MTL::CommandBuffer* completed) {
					if (completed->status() == MTL::CommandBufferStatusError)
						spdlog::error("crowd compute: {}", DescriptionOf(completed->error()));
				});
				cmdBuffer->commit();
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
				return m_Event->signaledValue();
			}

			void
			Wait() noexcept override
			{
				// The CPU wait is bounded, so a timeout is looped over rather than read as done.
				while (m_Event->signaledValue() < m_Submitted)
					m_Event->waitUntilSignaledValue(m_Submitted, 5000);
			}

			std::span<const uint32_t>
			GetReadback() const override
			{
				if (m_Submitted == 0 || InFlight())
					core::throw_runtime_error("The hash fill has no finished result");
				return { static_cast<const uint32_t*>(m_Readback->contents()), m_Count };
			}

		private:
			bgpu::GpuContextRef m_Context;
			uint32_t            m_Count           = 0;
			uint32_t            m_ParamsIndex     = 0;
			uint32_t            m_OutputIndex     = 0;
			uint32_t            m_ThreadsPerGroup = 1;
			uint64_t            m_Submitted       = 0;

			NS::SharedPtr<MTL::CommandQueue>         m_Queue;
			NS::SharedPtr<MTL::SharedEvent>          m_Event;
			NS::SharedPtr<MTL::ComputePipelineState> m_Pipeline;
			NS::SharedPtr<MTL::Buffer>               m_Output;
			NS::SharedPtr<MTL::Buffer>               m_Readback;
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
