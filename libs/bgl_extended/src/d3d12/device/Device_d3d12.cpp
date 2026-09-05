#include "device/Device_d3d12.h"
#include "RenderTarget_d3d12.h"
#include "cmd/CommandAllocator.h"
#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_d3d12.h"
#include "cmd/CommandQueue.h"
#include "cmd/CommandQueue_d3d12.h"
#include "cmd/TimestampHeap.h"
#include "cmd/TimestampHeap_d3d12.h"
#include "pipeline/ComputePipeline.h"
#include "pipeline/ComputePipeline_d3d12.h"
#include "pipeline/MeshletPipeline.h"
#include "pipeline/MeshletPipeline_d3d12.h"
#include "resource/ResourceManager.h"
#include "resource/ResourceManager_d3d12.h"
#include "resource/Shader.h"
#include "shadercache/ShaderCache_d3d12.h"
#include "types/QueueType.h"
#include <bgl_common/SlangErrorChecker.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	namespace
	{
		const char* const c_ShaderSearchPaths[] = { "./shaders/src", "./shaders/tests" };

		// Compile options that change generated code, folded into every cache key so a
		// compiler upgrade or a debug/release switch never reuses stale binaries.
		std::string
		ShaderCacheSalt()
		{
			// The free function, not IGlobalSession::getBuildTagString: same string, no session.
			std::string salt = spGetBuildTagString();
			salt += "|sm_6_6|column-major";
#if defined(BERNINI_GPU_DEBUG)
			salt += "|gpu-debug";
#endif
			return salt;
		}
	}

	Device::Device(
		wrl::ComPtr<ID3D12Device> device,
		const std::string&        shaderCacheDir,
		bool                      gpuValidation) :
		m_Device(std::move(device)), m_Slang(SlangSessionDesc{ SLANG_DXIL, c_ShaderSearchPaths })
	{
		gassert(m_Device != nullptr, "D3D12 device cannot be null");

		if (!shaderCacheDir.empty())
		{
			m_ShaderCache = std::make_unique<ShaderCache>(
				m_Device.Get(),
				shaderCacheDir,
				ShaderCacheSalt(),
				std::vector<std::string>(
					std::begin(c_ShaderSearchPaths),
					std::end(c_ShaderSearchPaths)),
				!gpuValidation);
		}
	}

	void
	Device::ReleaseSlangSession() noexcept
	{
		m_Slang.ReleaseAll();
	}

	Device::~Device() noexcept { logger::trace("~Device"); }

	CommandListRef
	Device::CreateCommandList(
		const CommandListDesc& desc,
		CommandAllocatorRef    commandAllocator,
		ResourceManagerRef     resourceManager) const noexcept
	{
		return core::SharedRef<CommandList>::Make(
			desc,
			std::move(commandAllocator),
			std::move(resourceManager));
	}

	ResourceManagerRef
	Device::CreateResourceManager(const ResourceManagerDesc& desc) const noexcept
	{
		return core::SharedRef<ResourceManager>::Make(m_Device, desc);
	}

	RenderTargetRef
	Device::CreateRenderTarget(
		const RenderTargetDesc&           desc,
		core::SharedRef<ICommandQueue>    queue,
		core::SharedRef<IResourceManager> resourceManager,
		bool                              enableDebug) const
	{
		return core::SharedRef<RenderTarget>::Make(
			desc,
			DeviceRef(const_cast<Device*>(this)),
			std::move(queue),
			std::move(resourceManager),
			enableDebug);
	}

	ShaderRef
	Device::CreateShader(ShaderDesc desc) const noexcept
	{
		return core::SharedRef<Shader>::Make(std::move(desc), &m_Slang);
	}

	MeshletPipelineRef
	Device::CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept
	{
		return core::SharedRef<MeshletPipeline>::Make(m_Device.Get(), m_ShaderCache.get(), desc);
	}

	ComputePipelineRef
	Device::CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept
	{
		return core::SharedRef<ComputePipeline>::Make(m_Device.Get(), m_ShaderCache.get(), desc);
	}

	CommandAllocatorRef
	Device::CreateCommandAllocator(QueueType type) const noexcept
	{
		auto d3d12CmdAllocator = wrl::ComPtr<ID3D12CommandAllocator>();

		m_Device->CreateCommandAllocator(
			ConvertQueueType(type),
			IID_PPV_ARGS(&d3d12CmdAllocator)) >>
			d3d12ErrChecker;

		return core::SharedRef<CommandAllocator>::Make(std::move(d3d12CmdAllocator));
	}

	CommandQueueRef
	Device::CreateCommandQueue(QueueType type) const noexcept
	{
		return core::SharedRef<CommandQueue>::Make(type, m_Device.Get());
	}

	core::SharedRef<ITimestampHeap>
	Device::CreateTimestampHeap(uint32_t capacity) const noexcept
	{
		return core::SharedRef<TimestampHeap>::Make(m_Device.Get(), capacity);
	}

	Uniforms
	Device::CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
		const noexcept
	{
		gassert(pipeline != nullptr, "Pipeline pointer cannot be null");
		return Uniforms(pipeline, cbufferName);
	}

	Uniforms
	Device::CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
		const noexcept
	{
		gassert(pipeline != nullptr, "Pipeline pointer cannot be null");
		return Uniforms(pipeline, cbufferName);
	}
}
