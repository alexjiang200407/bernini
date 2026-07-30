#include "device/Device_metal.h"

#include "cmd/CommandAllocator_metal.h"
#include "cmd/CommandList_metal.h"
#include "cmd/CommandQueue_metal.h"
#include "pipeline/ComputePipeline_metal.h"
#include "pipeline/MeshletPipeline_metal.h"
#include "resource/ResourceManager_metal.h"
#include "resource/Shader_metal.h"

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
	}

	Device::Device(MTL::Device* device) : m_Device(NS::RetainPtr(device))
	{
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

		m_SlangGlobalSession->createSession(sessionDesc, m_SlangSession.writeRef());
		gassert(m_SlangSession != nullptr, "Failed to create Slang session");
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
	Device::CreateResourceManager(const ResourceManagerDesc& desc, core::SharedRef<ICommandQueue>)
		const noexcept
	{
		return core::SharedRef<ResourceManager>::Make(m_Device.get(), desc);
	}

	core::SharedRef<IShader>
	Device::CreateShader(ShaderDesc desc) const noexcept
	{
		return core::SharedRef<Shader>::Make(std::move(desc), m_SlangSession.get());
	}

	core::SharedRef<IComputePipeline>
	Device::CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept
	{
		return core::SharedRef<ComputePipeline>::Make(m_Device.get(), m_SlangSession.get(), desc);
	}

	core::SharedRef<IMeshletPipeline>
	Device::CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept
	{
		return core::SharedRef<MeshletPipeline>::Make(m_Device.get(), m_SlangSession.get(), desc);
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
