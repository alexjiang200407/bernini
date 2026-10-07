#pragma once
#include "volk_vulkan.h"
#include <atomic>
#include <bgpu/GpuContext.h>
#include <core/ref/Ref.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <string_view>

namespace bgpu
{
	/**
	 * A VkImage and the one device-local allocation behind it, as D3D12 commits one resource per
	 * texture.
	 *
	 * Ref-counted for the same reason BufferMemory is: Vulkan counts no references to an image, and
	 * another owner on the same context may adopt it (ImportNativeTexture), so the image lives until
	 * the last owner lets go. It holds the context, so the device outlives it.
	 */
	class ImageMemory final : public core::RefCounter<core::Ref>
	{
	public:
		/** @throws std::runtime_error when the image or its memory cannot be created. */
		ImageMemory(
			GpuContextRef            context,
			const VkImageCreateInfo& info,
			std::string_view         debugName);
		~ImageMemory() noexcept override;

		ImageMemory(const ImageMemory&) = delete;
		ImageMemory(ImageMemory&&)      = delete;
		ImageMemory&
		operator=(const ImageMemory&) = delete;
		ImageMemory&
		operator=(ImageMemory&&) = delete;

		/**
		 * The image a bgpu manager on any context made as `image`, with a reference added; null for
		 * an image none made.
		 *
		 * @pre the owner of `image` keeps it alive for the duration of the call.
		 */
		[[nodiscard]] static core::SharedRef<ImageMemory>
		Find(VkImage image) noexcept;

		[[nodiscard]] VkImage
		GetVkImage() const noexcept
		{
			return m_Image;
		}

		/** What the allocation holds, alignment and metadata included. */
		[[nodiscard]] uint64_t
		GetAllocationSize() const noexcept
		{
			return m_AllocationSize;
		}

		[[nodiscard]] const GpuContextRef&
		GetContext() const noexcept
		{
			return m_Context;
		}

		/**
		 * Whether the image still owes its transition out of `UNDEFINED`, and the one caller that
		 * clears it records the transition. Kept here, not in a manager, because every manager that
		 * holds the image may be the first to submit work that uses it.
		 */
		[[nodiscard]] bool
		AwaitsInitialLayout() const noexcept
		{
			return m_AwaitsInitialLayout.load(std::memory_order_acquire);
		}

		void
		AwaitInitialLayout() noexcept
		{
			m_AwaitsInitialLayout.store(true, std::memory_order_release);
		}

		[[nodiscard]] bool
		ClaimInitialLayout() noexcept
		{
			return m_AwaitsInitialLayout.exchange(false, std::memory_order_acq_rel);
		}

	private:
		void
		Destroy() noexcept;

		GpuContextRef  m_Context;
		VkDevice       m_Device         = VK_NULL_HANDLE;
		VkImage        m_Image          = VK_NULL_HANDLE;
		VkDeviceMemory m_Memory         = VK_NULL_HANDLE;
		uint64_t       m_AllocationSize = 0;

		std::atomic<bool> m_AwaitsInitialLayout = false;
	};
}
