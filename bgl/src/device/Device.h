#pragma once
#include "types/QueueType.h"
#include <core/file/file.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class IResourceManager;
	class IShader;
	class IGraphicsPipeline;
	class ICommandList;
	class ICommandAllocator;
	class ICommandQueue;
	struct ShaderDesc;
	struct GraphicsPipelineDesc;
	struct CommandListDesc;

	class IDevice : public core::Ref
	{
	public:
		[[nodiscard]]
		virtual core::SharedRef<IShader>
		CreateShader(const ShaderDesc& desc) const = 0;

		[[nodiscard]]
		virtual core::SharedRef<IShader>
		CreateShader(ShaderDesc&& desc) const = 0;

		[[nodiscard]] core::SharedRef<IShader>
		CreateShader(std::string_view sv) const;

		[[nodiscard]]
		virtual core::SharedRef<IGraphicsPipeline>
		CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) const = 0;

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
	};

	using DeviceHandle = core::SharedRef<IDevice>;
}
