#include "convert_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/types/Barrier.h>
#include <bgpu/types/QueueType.h>
#include <core/err/util.h>
#include <cstdint>
#include <span>
#include <vector>

namespace bgpu
{
	VkPipelineStageFlags2
	ConvertBarrierSync(const BarrierSync sync) noexcept
	{
		core::ensure(
			!(sync & BarrierSyncFlag::kRayTracing),
			"Ray tracing is not part of the Vulkan bar");

		VkPipelineStageFlags2 result = VK_PIPELINE_STAGE_2_NONE;
		if (sync & BarrierSyncFlag::kAllCommands)
			result |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		if (sync & BarrierSyncFlag::kCopy)
			result |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
		if (sync & BarrierSyncFlag::kResolve)
			result |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
		if (sync & BarrierSyncFlag::kInputAssembler)
			result |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
		// D3D12's vertex shading is every stage before the rasterizer, the mesh stages included.
		if (sync & BarrierSyncFlag::kVertexShader)
			result |= VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT;
		if (sync & BarrierSyncFlag::kPixelShader)
			result |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		if (sync & BarrierSyncFlag::kComputeShader)
			result |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		if (sync & BarrierSyncFlag::kRenderTarget)
			result |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		if (sync & BarrierSyncFlag::kDepthStencil)
		{
			result |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
			          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		}
		if (sync & BarrierSyncFlag::kIndirectArgument)
			result |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
		return result;
	}

	VkAccessFlags2
	ConvertBarrierAccess(const BarrierAccess access) noexcept
	{
		core::ensure(
			!(access & BarrierAccessFlag::kAccelStructRead) &&
				!(access & BarrierAccessFlag::kAccelStructWrite),
			"Ray tracing is not part of the Vulkan bar");

		VkAccessFlags2 result = VK_ACCESS_2_NONE;
		if (access & BarrierAccessFlag::kCommon)
			result |= VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		if (access & BarrierAccessFlag::kIndexBuffer)
			result |= VK_ACCESS_2_INDEX_READ_BIT;
		if (access & BarrierAccessFlag::kVertexBuffer)
			result |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
		if (access & BarrierAccessFlag::kConstantBuffer)
			result |= VK_ACCESS_2_UNIFORM_READ_BIT;
		if (access & BarrierAccessFlag::kShaderResource)
			result |= VK_ACCESS_2_SHADER_READ_BIT;
		if (access & BarrierAccessFlag::kUnorderedAccess)
			result |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
		if (access & BarrierAccessFlag::kRenderTarget)
		{
			result |=
				VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		}
		if (access & BarrierAccessFlag::kDepthWrite)
		{
			result |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}
		if (access & BarrierAccessFlag::kDepthRead)
			result |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		if (access & BarrierAccessFlag::kIndirectArgument)
			result |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
		if (access & BarrierAccessFlag::kCopySource)
			result |= VK_ACCESS_2_TRANSFER_READ_BIT;
		if (access & BarrierAccessFlag::kCopyDest)
			result |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
		return result;
	}

	std::vector<uint32_t>
	QueueFamiliesFor(const QueueType type, const std::span<const VkQueueFamilyProperties> families)
	{
		enum class FamilyKind : uint8_t
		{
			kNone,
			kTransferOnly,
			kComputeOnly,
			kGraphicsCompute,
		};

		// Graphics and compute families support transfers whether or not they say so.
		const auto kindOf = [](const VkQueueFlags flags) {
			const bool graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
			const bool compute  = (flags & VK_QUEUE_COMPUTE_BIT) != 0;
			if (graphics && compute)
				return FamilyKind::kGraphicsCompute;
			if (compute)
				return FamilyKind::kComputeOnly;
			if ((flags & VK_QUEUE_TRANSFER_BIT) != 0)
				return FamilyKind::kTransferOnly;
			return FamilyKind::kNone;
		};

		auto order = std::vector<FamilyKind>();
		switch (type)
		{
		case QueueType::kGraphics:
			order = { FamilyKind::kGraphicsCompute };
			break;
		case QueueType::kCompute:
			order = { FamilyKind::kComputeOnly, FamilyKind::kGraphicsCompute };
			break;
		case QueueType::kCopy:
			order = { FamilyKind::kTransferOnly,
				      FamilyKind::kComputeOnly,
				      FamilyKind::kGraphicsCompute };
			break;
		}

		auto result = std::vector<uint32_t>();
		for (const FamilyKind wanted : order)
		{
			for (uint32_t i = 0; i < families.size(); ++i)
			{
				if (families[i].queueCount > 0 && kindOf(families[i].queueFlags) == wanted)
					result.push_back(i);
			}
		}
		core::ensure(!result.empty(), "The Vulkan device has no queue family for this queue type");
		return result;
	}
}
