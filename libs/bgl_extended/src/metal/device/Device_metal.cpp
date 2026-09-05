#include "device/Device_metal.h"

#include "RenderTarget_metal.h"

#include "cmd/CommandAllocator_metal.h"
#include "cmd/CommandList_metal.h"
#include "cmd/CommandQueue_metal.h"
#include "cmd/TimestampHeap.h"
#include "cmd/TimestampHeap_metal.h"
#include "device/Device.h"
#include "pipeline/ComputePipeline_metal.h"
#include "pipeline/MeshletPipeline_metal.h"
#include "resource/ResourceManager_metal.h"
#include "shadercache/ShaderCache_metal.h"
#include <bgl/IRenderTarget.h>
#include <core/ref/SharedRef.h>

#include "cmd/CommandList.h"
#include "pipeline/ComputePipeline.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "slang/SlangSessions.h"
#include "types/QueueType.h"
#include "uniforms/Uniforms.h"
#include <cstdint>
#include <iterator>
#include <memory>
#include <slang.h>
#include <string>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		const char* const c_ShaderSearchPaths[] = { "./shaders/src", "./shaders/tests" };

		// Compile options that change generated code, folded into every cache key so a compiler
		// upgrade or a debug/release switch never reuses stale binaries.
		std::string
		ShaderCacheSalt()
		{
			// The free function, not IGlobalSession::getBuildTagString: same string, no session.
			std::string salt = spGetBuildTagString();
			salt += "|metal|sm_6_6|column-major";
#if defined(BERNINI_GPU_DEBUG)
			salt += "|gpu-debug";
#endif
			return salt;
		}
	}

	Device::~Device() = default;

	Device::Device(
		MTL::Device*       device,
		const std::string& shaderCacheDir,
		bool               usePipelineLibrary) :
		m_Device(NS::RetainPtr(device)),
		m_Slang(SlangSessionDesc{ SLANG_METAL, c_ShaderSearchPaths })
	{
		if (!shaderCacheDir.empty())
		{
			m_ShaderCache = std::make_unique<ShaderCache>(
				m_Device.get(),
				shaderCacheDir,
				ShaderCacheSalt(),
				std::vector<std::string>(
					std::begin(c_ShaderSearchPaths),
					std::end(c_ShaderSearchPaths)),
				usePipelineLibrary);
		}
	}

	void
	Device::ReleaseSlangSession() noexcept
	{
		m_Slang.ReleaseAll();
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
		if (!m_Device->supportsCounterSampling(MTL::CounterSamplingPointAtStageBoundary))
		{
			logger::warn("CreateTimestampHeap: the device cannot sample at a stage boundary");
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
			logger::warn("CreateTimestampHeap: the device has no timestamp counter set");
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
			logger::warn(
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
		return core::SharedRef<Shader>::Make(std::move(desc), &m_Slang);
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
