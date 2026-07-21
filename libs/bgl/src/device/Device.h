#pragma once
#include "pipeline/ComputeKernel.h"
#include "pipeline/MeshletKernel.h"
#include "types/QueueType.h"
#include "uniforms/Uniforms.h"
#include <core/file/file.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class IResourceManager;
	class IShader;
	class IMeshletPipeline;
	class ICommandList;
	class IComputePipeline;
	class ICommandAllocator;
	class ICommandQueue;
	struct ShaderDesc;
	struct MeshletPipelineDesc;
	struct ComputePipelineDesc;
	struct CommandListDesc;
	struct ResourceManagerDesc;

	class IDevice : public core::Ref
	{
	public:
		IDevice()                        = default;
		IDevice(const IDevice&) noexcept = delete;
		IDevice(IDevice&&) noexcept      = delete;

		IDevice&
		operator=(const IDevice&) noexcept = delete;

		IDevice&
		operator=(IDevice&&) noexcept = delete;

		[[nodiscard]]
		virtual core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const noexcept = 0;

		[[nodiscard]] core::SharedRef<IShader>
		CreateShader(std::string slangModuleName, std::string entryPointName = "main")
			const noexcept;

		[[nodiscard]]
		virtual core::SharedRef<IComputePipeline>
		CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept = 0;

		[[nodiscard]]
		virtual core::SharedRef<IMeshletPipeline>
		CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept = 0;

		[[nodiscard]] ComputeKernel
		CreateComputeKernel(const ComputePipelineDesc& desc) const noexcept;

		[[nodiscard]] MeshletKernel
		CreateMeshletKernel(const MeshletPipelineDesc& desc) const noexcept;

		virtual core::SharedRef<ICommandList>
		CreateCommandList(
			const CommandListDesc&             desc,
			core::SharedRef<ICommandAllocator> commandAllocator,
			core::SharedRef<IResourceManager>  resourceManager) const noexcept = 0;

		[[nodiscard]]
		core::SharedRef<ICommandQueue>
		CreateGraphicsCommandQueue() const noexcept;

		[[nodiscard]]
		virtual core::SharedRef<ICommandAllocator>
		CreateCommandAllocator(QueueType type = QueueType::kGraphics) const noexcept = 0;

		[[nodiscard]]
		virtual core::SharedRef<ICommandQueue>
		CreateCommandQueue(QueueType type) const noexcept = 0;

		[[nodiscard]]
		virtual core::SharedRef<IResourceManager>
		CreateResourceManager(const ResourceManagerDesc& desc) const noexcept = 0;

		[[nodiscard]]
		virtual Uniforms
		CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
			const noexcept = 0;

		[[nodiscard]]
		virtual Uniforms
		CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
			const noexcept = 0;
	};

	using DeviceRef = core::SharedRef<IDevice>;
}
