#pragma once
#include "types/Format.h"
#include <assetlib_structs/VkFormat.h>

namespace bgl
{
	/**
	 * The engine format for the format tag a KTX2 container carries. gfatal on a tag no backend
	 * supports.
	 */
	[[nodiscard]] Format
	FromVkFormat(assetlib::VkFormat vkFormat) noexcept;
}
