#pragma once
#include <assetlib_structs/VkFormat.h>
#include <bgpu/types/Format.h>

namespace bgl
{
	/**
	 * The engine format for the format tag a KTX2 container carries. core::fatal on a tag no backend
	 * supports.
	 */
	[[nodiscard]] Format
	FromVkFormat(assetlib::VkFormat vkFormat) noexcept;
}
