#include "device/Device_metal.h"

#include "RenderTarget_metal.h"

#include "cmd/CommandAllocator_metal.h"
#include "cmd/CommandList_metal.h"
#include "cmd/CommandQueue_metal.h"
#include "pipeline/ComputePipeline_metal.h"
#include "pipeline/MeshletPipeline_metal.h"
#include "resource/ResourceManager_metal.h"
#include "resource/Shader_metal.h"
#include "shadercache/ShaderCache_metal.h"
#include "uniforms/Uniforms_metal.h"

#include "cmd/CommandList.h"
#include "pipeline/ComputePipeline.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"

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

	Device::Device(MTL::Device* device, const std::string& shaderCacheDir) :
		m_Device(NS::RetainPtr(device))
	{
		if (!shaderCacheDir.empty())
		{
			m_ShaderCache = std::make_unique<ShaderCache>(
				m_Device.get(),
				shaderCacheDir,
				ShaderCacheSalt(),
				std::vector<std::string>(
					std::begin(c_ShaderSearchPaths),
					std::end(c_ShaderSearchPaths)));
		}
	}

	slang::ISession*
	Device::GetSlangSession() const noexcept
	{
		if (m_SlangSession != nullptr)
			return m_SlangSession.get();

		slang::createGlobalSession(m_SlangGlobalSession.writeRef());
		gassert(m_SlangGlobalSession != nullptr, "Failed to create Slang global session");

		slang::SessionDesc sessionDesc = {};
		slang::TargetDesc  targetDesc  = {};

		targetDesc.format  = SLANG_METAL;
		targetDesc.profile = m_SlangGlobalSession->findProfile("sm_6_6");

		sessionDesc.targetCount     = 1;
		sessionDesc.targets         = &targetDesc;
		sessionDesc.searchPaths     = c_ShaderSearchPaths;
		sessionDesc.searchPathCount = std::size(c_ShaderSearchPaths);
		// Match the column-major convention the CPU side uploads matrices in (see Device_d3d12).
		sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

#if defined(BERNINI_GPU_DEBUG)
		// Enables dbg_raise() and the cull-stats counters in runtime-compiled shaders, as
		// Device_d3d12 does. Without it every guarded block compiles out and the counters read
		// zero while everything around them works.
		const slang::PreprocessorMacroDesc debugMacro = { "BERNINI_GPU_DEBUG", "1" };
		sessionDesc.preprocessorMacros                = &debugMacro;
		sessionDesc.preprocessorMacroCount            = 1;
#endif

		m_SlangGlobalSession->createSession(sessionDesc, m_SlangSession.writeRef());
		gassert(m_SlangSession != nullptr, "Failed to create Slang session");

		return m_SlangSession.get();
	}

	void
	Device::ReleaseSlangSession() noexcept
	{
		m_SlangSession.setNull();
		m_SlangGlobalSession.setNull();
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
		return core::SharedRef<Shader>::Make(std::move(desc), GetSlangSession());
	}

	core::SharedRef<IComputePipeline>
	Device::CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept
	{
		return core::SharedRef<ComputePipeline>::Make(
			m_Device.get(),
			GetSlangSession(),
			m_ShaderCache.get(),
			desc);
	}

	core::SharedRef<IMeshletPipeline>
	Device::CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept
	{
		return core::SharedRef<MeshletPipeline>::Make(
			m_Device.get(),
			GetSlangSession(),
			m_ShaderCache.get(),
			desc);
	}

	UniformsRef
	Device::CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
		const noexcept
	{
		return core::SharedRef<Uniforms>::Make(pipeline, cbufferName);
	}

	UniformsRef
	Device::CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
		const noexcept
	{
		return core::SharedRef<Uniforms>::Make(pipeline, cbufferName);
	}
}
