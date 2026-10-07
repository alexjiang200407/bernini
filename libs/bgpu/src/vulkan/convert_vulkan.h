#pragma once
#include "volk_vulkan.h"
#include <bgpu/resource/Sampler.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/types/TextureDimension.h>
#include <cstdint>
#include <span>
#include <vector>

namespace bgpu
{
	/**
	 * The Vulkan format `format` is stored as. `D24S8` is `D32_SFLOAT_S8_UINT`, as Metal makes it,
	 * since AMD has no 24-bit depth; a stencil view's `X24G8_UINT` and `X32G8_UINT` are that same
	 * depth-stencil format, read through its stencil aspect.
	 */
	[[nodiscard]] VkFormat
	ConvertFormat(Format format) noexcept;

	/** The aspects an image of `format` has: colour, or its depth and stencil. */
	[[nodiscard]] VkImageAspectFlags
	FormatAspects(Format format) noexcept;

	/**
	 * The one aspect a shader view of `viewFormat` reads on an image of `imageAspects`: colour on a
	 * colour image; on a depth image, stencil for `X24G8_UINT` and `X32G8_UINT` and depth for any
	 * other format, `R32_FLOAT` included.
	 */
	[[nodiscard]] VkImageAspectFlags
	ViewAspect(VkImageAspectFlags imageAspects, Format viewFormat) noexcept;

	/**
	 * Every layout but `kUndefined` is `GENERAL`, the one layout valid for every copy, clear,
	 * attachment and descriptor, so nothing has to know the layout a texture is in. `kPresent` is a
	 * swapchain's, and ends the process until there is one.
	 */
	[[nodiscard]] VkImageLayout
	ConvertImageLayout(BarrierLayout layout) noexcept;

	[[nodiscard]] VkImageViewType
	ConvertImageViewType(TextureDimension dimension) noexcept;

	/** A sampler's create info, with `reduction` chained to it for a minimum or maximum filter. */
	[[nodiscard]] VkSamplerCreateInfo
	ConvertSamplerDesc(
		const SamplerDesc&                desc,
		VkSamplerReductionModeCreateInfo& reduction) noexcept;

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
