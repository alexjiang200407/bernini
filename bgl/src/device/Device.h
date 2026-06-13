#pragma once
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
	class ICommandAllocator;
	class ICommandQueue;
	struct ShaderDesc;
	struct MeshletPipelineDesc;
	struct CommandListDesc;

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
		CreateShader(ShaderDesc desc) const = 0;

		[[nodiscard]] core::SharedRef<IShader>
		CreateShader(std::string path, std::string moduleName) const;

		[[nodiscard]]
		virtual core::SharedRef<IMeshletPipeline>
		CreateMeshletPipeline(const MeshletPipelineDesc& desc) const = 0;

		virtual core::SharedRef<ICommandList>
		CreateCommandList(
			const CommandListDesc&             desc,
			core::SharedRef<ICommandAllocator> commandAllocator,
			core::SharedRef<IResourceManager>  resourceManager) const = 0;

		[[nodiscard]]
		core::SharedRef<ICommandQueue>
		CreateGraphicsCommandQueue() const;

		[[nodiscard]]
		virtual core::SharedRef<ICommandAllocator>
		CreateCommandAllocator() const = 0;

		[[nodiscard]]
		virtual core::SharedRef<ICommandQueue>
		CreateCommandQueue(QueueType type) const = 0;

		[[nodiscard]]
		virtual core::SharedRef<IResourceManager>
		CreateResourceManager(uint32_t maxCbvSrvUav, uint32_t maxRtvs) const = 0;

		[[nodiscard]]
		virtual Uniforms
		CreateUniforms(IMeshletPipeline const* pipeline) const = 0;
	};

	using DeviceHandle = core::SharedRef<IDevice>;
}
