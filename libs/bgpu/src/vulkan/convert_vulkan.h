#pragma once
#include "volk_vulkan.h"
#include <bgpu/types/Barrier.h>
#include <bgpu/types/QueueType.h>
#include <cstdint>
#include <span>
#include <vector>

namespace bgpu
{
	/** The synchronization2 stages a D3D12 enhanced barrier's sync names. */
	[[nodiscard]] VkPipelineStageFlags2
	ConvertBarrierSync(BarrierSync sync) noexcept;

	/** The synchronization2 accesses a D3D12 enhanced barrier's access names. */
	[[nodiscard]] VkAccessFlags2
	ConvertBarrierAccess(BarrierAccess access) noexcept;

	/**
	 * The families a queue of `type` may come from, best first, as D3D12 picks a queue by type: the
	 * graphics family for graphics; a family with compute and no graphics for compute, then the
	 * graphics family; a transfer-only family for copies, then the compute and graphics ones.
	 */
	[[nodiscard]] std::vector<uint32_t>
	QueueFamiliesFor(QueueType type, std::span<const VkQueueFamilyProperties> families);
}
