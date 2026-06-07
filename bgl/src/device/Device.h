#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "pipeline/GraphicsPipeline.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "types/QueueType.h"

#include <core/file/file.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class IDevice : public core::Ref
	{
	public:
		[[nodiscard]]
		virtual ShaderHandle
		CreateShader(const ShaderDesc& desc) const = 0;

		[[nodiscard]]
		virtual ShaderHandle
		CreateShader(ShaderDesc&& desc) const = 0;

		[[nodiscard]] ShaderHandle
		CreateShader(std::string_view sv) const
		{
			auto desc      = ShaderDesc();
			desc.bytecode  = core::file::readFileBytes(sv);
			desc.debugName = sv;
			return CreateShader(std::move(desc));
		}

		[[nodiscard]]
		virtual GraphicsPipelineHandle
		CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) const = 0;

		[[nodiscard]]
		CommandListHandle
		CreateGraphicsCommandList(
			CommandAllocatorHandle commandAllocator,
			ResourceManagerHandle  resourceManager) const
		{
			return CreateCommandList(
				QueueType::kGraphics,
				std::move(commandAllocator),
				std::move(resourceManager));
		}

		virtual CommandListHandle
		CreateCommandList(
			QueueType              type,
			CommandAllocatorHandle commandAllocator,
			ResourceManagerHandle  resourceManager) const = 0;

		[[nodiscard]]
		CommandQueueHandle
		CreateGraphicsCommandQueue() const
		{
			return CreateCommandQueue(QueueType::kGraphics);
		}

		[[nodiscard]]
		virtual CommandAllocatorHandle
		CreateCommandAllocator() const = 0;

		[[nodiscard]]
		virtual CommandQueueHandle
		CreateCommandQueue(QueueType type) const = 0;

		[[nodiscard]]
		virtual ResourceManagerHandle
		CreateResourceManager(uint32_t maxCbvSrvUav, uint32_t maxRtvs) const = 0;
	};

	using DeviceHandle = core::SharedRef<IDevice>;
}
