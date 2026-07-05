#pragma once
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
#include "slang/ErrorChecker.h"
#include "types/QueueType.h"
#include <core/ref/SharedRef.h>

namespace bgl
{
	Device::Device(
		wrl::ComPtr<ID3D12Device>            device,
		Slang::ComPtr<slang::IGlobalSession> globalSession) :
		m_Device(std::move(device)), m_SlangGlobalSession(std::move(globalSession))
	{
		gassert(m_Device != nullptr, "D3D12 device cannot be null");
		gassert(m_SlangGlobalSession != nullptr, "Slang global session cannot be null");

		slang::SessionDesc sessionDesc = {};
		slang::TargetDesc  targetDesc  = {};

		targetDesc.format  = SLANG_DXIL;
		targetDesc.profile = m_SlangGlobalSession->findProfile("sm_6_6");

		const char* searchPaths[] = { "./shaders/src", "./shaders/tests" };

		sessionDesc.targetCount     = 1;
		sessionDesc.targets         = &targetDesc;
		sessionDesc.searchPaths     = searchPaths;
		sessionDesc.searchPathCount = std::size(searchPaths);

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
	}

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
	Device::CreateResourceManager(const ResourceManagerDesc& desc) const noexcept
	{
		return core::SharedRef<ResourceManager>::Make(m_Device, desc);
	}

	ShaderHandle
	Device::CreateShader(ShaderDesc desc) const noexcept
	{
		return core::SharedRef<Shader>::Make(std::move(desc), m_SlangSession);
	}

	MeshletPipelineHandle
	Device::CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept
	{
		return core::SharedRef<MeshletPipeline>::Make(m_Device.Get(), m_SlangSession.get(), desc);
	}

	ComputePipelineHandle
	Device::CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept
	{
		return core::SharedRef<ComputePipeline>::Make(m_Device.Get(), m_SlangSession.get(), desc);
	}

	CommandAllocatorHandle
	Device::CreateCommandAllocator() const noexcept
	{
		auto d3d12CmdAllocator = wrl::ComPtr<ID3D12CommandAllocator>();

		m_Device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
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
