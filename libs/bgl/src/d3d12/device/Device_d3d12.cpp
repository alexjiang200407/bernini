#include "device/Device_d3d12.h"
#include "cmd/CommandAllocator.h"
#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_d3d12.h"
#include "cmd/CommandQueue.h"
#include "cmd/CommandQueue_d3d12.h"
#include "pipeline/ComputePipeline.h"
#include "pipeline/ComputePipeline_d3d12.h"
#include "pipeline/MeshletPipeline.h"
#include "pipeline/MeshletPipeline_d3d12.h"
#include "resource/ResourceManager.h"
#include "resource/ResourceManager_d3d12.h"
#include "resource/Shader.h"
#include "resource/Shader_d3d12.h"
#include "shadercache/ShaderCache_d3d12.h"
#include "slang/ErrorChecker.h"
#include "types/QueueType.h"
#include <core/ref/SharedRef.h>

namespace bgl
{
	namespace
	{
		const char* const c_ShaderSearchPaths[] = { "./shaders/src", "./shaders/tests" };

		// Compile options that change generated code, folded into every cache key so a
		// compiler upgrade or a debug/release switch never reuses stale binaries.
		std::string
		ShaderCacheSalt(slang::IGlobalSession* globalSession)
		{
			std::string salt = globalSession->getBuildTagString();
			salt += "|sm_6_6|column-major";
#if defined(BERNINI_GPU_DEBUG)
			salt += "|gpu-debug";
#endif
			return salt;
		}
	}

	Device::Device(
		wrl::ComPtr<ID3D12Device>            device,
		Slang::ComPtr<slang::IGlobalSession> globalSession,
		const std::string&                   shaderCacheDir) :
		m_Device(std::move(device)), m_SlangGlobalSession(std::move(globalSession))
	{
		gassert(m_Device != nullptr, "D3D12 device cannot be null");
		gassert(m_SlangGlobalSession != nullptr, "Slang global session cannot be null");

		slang::SessionDesc sessionDesc = {};
		slang::TargetDesc  targetDesc  = {};

		targetDesc.format  = SLANG_DXIL;
		targetDesc.profile = m_SlangGlobalSession->findProfile("sm_6_6");

		const char* const* searchPaths = c_ShaderSearchPaths;

		sessionDesc.targetCount     = 1;
		sessionDesc.targets         = &targetDesc;
		sessionDesc.searchPaths     = searchPaths;
		sessionDesc.searchPathCount = std::size(c_ShaderSearchPaths);

		// Match the column-major convention the CPU side uploads matrices in (and that the
		// offline slangc default used). The Slang API's SessionDesc otherwise defaults to
		// row-major, which would transpose viewProj / transforms and project geometry off
		// screen once shaders are compiled through this session at PSO creation.
		sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

#if defined(BERNINI_GPU_DEBUG)
		// Enables dbg_raise() bodies in runtime-compiled shaders. Kept in lockstep
		// with the offline slangc -D in cmake/compile_shader.cmake. Fully absent in
		// Release, so gDebug drops out of reflection and dbg_raise becomes a no-op.
		const slang::PreprocessorMacroDesc debugMacro = { "BERNINI_GPU_DEBUG", "1" };
		sessionDesc.preprocessorMacros                = &debugMacro;
		sessionDesc.preprocessorMacroCount            = 1;
#endif

		SlangErrorChecker errChecker;
		m_SlangGlobalSession->createSession(sessionDesc, m_SlangSession.writeRef()) >> errChecker;

		gassert(m_SlangSession != nullptr, "Failed to create Slang session");

		if (!shaderCacheDir.empty())
		{
			m_ShaderCache = std::make_unique<ShaderCache>(
				m_Device.Get(),
				shaderCacheDir,
				ShaderCacheSalt(m_SlangGlobalSession.get()),
				std::vector<std::string>(
					std::begin(c_ShaderSearchPaths),
					std::end(c_ShaderSearchPaths)));
		}
	}

	Device::~Device() noexcept { logger::trace("~Device"); }

	CommandListHandle
	Device::CreateCommandList(
		const CommandListDesc& desc,
		CommandAllocatorHandle commandAllocator,
		ResourceManagerHandle  resourceManager) const noexcept
	{
		return core::SharedRef<CommandList>::Make(
			desc,
			std::move(commandAllocator),
			std::move(resourceManager));
	}

	ResourceManagerHandle
	Device::CreateResourceManager(
		const ResourceManagerDesc&     desc,
		core::SharedRef<ICommandQueue> submissionQueue) const noexcept
	{
		return core::SharedRef<ResourceManager>::Make(m_Device, desc, std::move(submissionQueue));
	}

	ShaderHandle
	Device::CreateShader(ShaderDesc desc) const noexcept
	{
		return core::SharedRef<Shader>::Make(std::move(desc), m_SlangSession);
	}

	MeshletPipelineHandle
	Device::CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept
	{
		return core::SharedRef<MeshletPipeline>::Make(
			m_Device.Get(),
			m_SlangSession.get(),
			m_ShaderCache.get(),
			desc);
	}

	ComputePipelineHandle
	Device::CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept
	{
		return core::SharedRef<ComputePipeline>::Make(
			m_Device.Get(),
			m_SlangSession.get(),
			m_ShaderCache.get(),
			desc);
	}

	CommandAllocatorHandle
	Device::CreateCommandAllocator(QueueType type) const noexcept
	{
		auto d3d12CmdAllocator = wrl::ComPtr<ID3D12CommandAllocator>();

		m_Device->CreateCommandAllocator(
			ConvertQueueType(type),
			IID_PPV_ARGS(&d3d12CmdAllocator)) >>
			d3d12ErrChecker;

		return core::SharedRef<CommandAllocator>::Make(std::move(d3d12CmdAllocator));
	}

	CommandQueueHandle
	Device::CreateCommandQueue(QueueType type) const noexcept
	{
		return core::SharedRef<CommandQueue>::Make(type, m_Device.Get());
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
