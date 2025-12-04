#include "GfxException.h"
#include <gfx/common.h>

namespace gfx
{
	GfxErrorInfo
	GfxException::GenerateErrorInfo(
		GfxResult res,
		std::string_view  title,
		std::string_view  body) noexcept
	{
		GfxErrorInfo info{};

		info.result = res;
		std::strncpy(info.title, title.data(), sizeof(info.title));
		std::strncpy(info.message, body.data(), sizeof(info.message));

		info.title[sizeof(info.title) - 1]     = '\0';
		info.message[sizeof(info.message) - 1] = '\0';

		return info;
	}

}
