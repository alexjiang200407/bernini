#include "device/Device_metal.h"

#include "RenderTarget_metal.h"

#include "cmd/CommandAllocator_metal.h"
#include "cmd/CommandList_metal.h"
#include "cmd/CommandQueue_metal.h"
#include "cmd/TimestampHeap_metal.h"
#include "pipeline/ComputePipeline_metal.h"
#include "pipeline/MeshletPipeline_metal.h"
#include "resource/ResourceManager_metal.h"
#include "shadercache/ShaderCache_metal.h"
#include <bgl/IRenderTarget.h>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/TimestampHeap.h>
#include <bgpu/device/Device.h>
#include <bgpu/metal/native_device.h>
#include <core/ref/SharedRef.h>

#include <bgpu/cmd/CommandList.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/uniforms/Uniforms.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <slang.h>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>
#include <vector>

namespace bgl
{
	Device::~Device() = default;

	Device::Device(const bgpu::GpuContextRef& context) :
		m_Context(context), m_Device(NS::RetainPtr(bgpu::GetMtlDevice(*context)))
	{
		if (m_Context->GetProgramCache() != nullptr)
		{
			m_ShaderCache = std::make_unique<ShaderCache>(
				m_Context,
				m_Device.get(),
				!m_Context->GpuValidationActive());
		}
	}

	void
	Device::AddSourceModule(const bgpu::SlangSourceModule& sourceModule) noexcept
	{
		m_Context->AddSourceModule(sourceModule);
	}

	void
	Device::ReleaseSlangSession() noexcept
	{
		m_Context->ReleaseSlangSessions();
	}

	core::SharedRef<ICommandQueue>
	Device::CreateCommandQueue(QueueType) const noexcept
	{
		return core::SharedRef<CommandQueue>::Make(m_Device.get());
	}

	core::SharedRef<ICommandAllocator>
	Device::CreateCommandAllocator(QueueType) const noexcept
	{
		return core::SharedRef<CommandAllocator>::Make();
	}

	core::SharedRef<ITimestampHeap>
	Device::CreateTimestampHeap(uint32_t capacity) const noexcept
	{
		// Apple GPUs sample at an encoder's stage boundary and nowhere finer, which is the point the
		// command list attaches a span's slots to; a device without even that has no timestamps.
		// Once: every render target asks at creation, and a device that cannot is the same device
		// every time.
		static bool g_Warned = false;
		const auto  warnOnce = [](const char* why) {
			if (!g_Warned)
			{
				spdlog::warn("CreateTimestampHeap: {}; no pass will be timed", why);
				g_Warned = true;
			}
		};

		if (!m_Device->supportsCounterSampling(MTL::CounterSamplingPointAtStageBoundary))
		{
			warnOnce("the device cannot sample at a stage boundary");
			return {};
		}

		const MTL::CounterSet* timestamps = nullptr;
		if (NS::Array* sets = m_Device->counterSets(); sets != nullptr)
		{
			for (NS::UInteger i = 0; i < sets->count(); ++i)
			{
				const auto* set = sets->object<MTL::CounterSet>(i);
				if (set->name()->isEqualToString(MTL::CommonCounterSetTimestamp))
				{
					timestamps = set;
					break;
				}
			}
		}

		if (timestamps == nullptr)
		{
			warnOnce("the device has no timestamp counter set");
			return {};
		}

		auto desc = NS::TransferPtr(MTL::CounterSampleBufferDescriptor::alloc()->init());
		desc->setCounterSet(timestamps);
		desc->setSampleCount(capacity);
		desc->setStorageMode(MTL::StorageModeShared);
		desc->setLabel(NS::String::string("Timestamp Heap", NS::UTF8StringEncoding));

		NS::Error* error  = nullptr;
		auto       buffer = NS::TransferPtr(m_Device->newCounterSampleBuffer(desc.get(), &error));
		if (buffer.get() == nullptr)
		{
			spdlog::warn(
				"CreateTimestampHeap: newCounterSampleBuffer failed: {}",
				error != nullptr && error->localizedDescription() != nullptr ?
					error->localizedDescription()->utf8String() :
					"no error given");
			return {};
		}

		return core::SharedRef<TimestampHeap>::Make(std::move(buffer), capacity);
	}

	core::SharedRef<ICommandList>
	Device::CreateCommandList(
		const CommandListDesc&             desc,
		core::SharedRef<ICommandAllocator> commandAllocator,
		core::SharedRef<IResourceManager>  resourceManager) const noexcept
	{
		return core::SharedRef<CommandList>::Make(
			desc,
			commandAllocator.Get(),
			std::move(resourceManager));
	}

	core::SharedRef<IResourceManager>
	Device::CreateResourceManager(const ResourceManagerDesc& desc) const noexcept
	{
		return core::SharedRef<ResourceManager>::Make(m_Device.get(), desc);
	}

	RenderTargetRef
	Device::CreateRenderTarget(
		const RenderTargetDesc&           desc,
		core::SharedRef<ICommandQueue>    queue,
		core::SharedRef<IResourceManager> resourceManager,
		bool) const
	{
		return core::SharedRef<RenderTarget>::Make(
			desc,
			core::SharedRef<IDevice>(const_cast<Device*>(this)),
			std::move(queue),
			std::move(resourceManager));
	}

	core::SharedRef<IShader>
	Device::CreateShader(ShaderDesc desc) const noexcept
	{
		return core::SharedRef<Shader>::Make(std::move(desc), m_Context);
	}

	core::SharedRef<IComputePipeline>
	Device::CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept
	{
		return core::SharedRef<ComputePipeline>::Make(m_Device.get(), m_ShaderCache.get(), desc);
	}

	core::SharedRef<IMeshletPipeline>
	Device::CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept
	{
		return core::SharedRef<MeshletPipeline>::Make(m_Device.get(), m_ShaderCache.get(), desc);
	}

	Uniforms
	Device::CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
		const noexcept
	{
		return Uniforms(pipeline, cbufferName);
	}

	Uniforms
	Device::CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
		const noexcept
	{
		return Uniforms(pipeline, cbufferName);
	}
}
