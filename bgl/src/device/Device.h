#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "pipeline/GraphicsPipeline.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "types/QueueType.h"
#include <core/file/file.h>
#include <core/pimpl/PImpl.h>

namespace bgl
{
	class DeviceImpl;
	class Device : public core::PImpl<DeviceImpl>
	{
	public:
		[[nodiscard]]
		Shader
		CreateShader(const ShaderDesc& desc) const;

		[[nodiscard]]
		Shader
		CreateShader(ShaderDesc&& desc) const;

		[[nodiscard]] Shader
		CreateShader(std::string_view sv) const
		{
			auto desc      = ShaderDesc();
			desc.bytecode  = core::file::readFileBytes(sv);
			desc.debugName = sv;
			return CreateShader(std::move(desc));
		}

		[[nodiscard]]
		GraphicsPipeline
		CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) const;

		[[nodiscard]]
		CommandList
		CreateGraphicsCommandList(
			CommandAllocator commandAllocator,
			ResourceManager  resourceManager) const;

		[[nodiscard]]
		CommandQueue
		CreateGraphicsCommandQueue(QueueType type) const;

		[[nodiscard]]
		CommandAllocator
		CreateCommandAllocator() const;

		[[nodiscard]]
		CommandQueue
		CreateCommandQueue(QueueType type = QueueType::kGraphics) const;

		[[nodiscard]]
		ResourceManager
		CreateResourceManager(uint32_t maxCbvSrvUav, uint32_t maxRtvs) const;

		friend class GraphicsImpl;
	};
}
