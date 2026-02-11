#include "error/GfxException.h"
#include <gfx/ffi/common.h>

namespace gfx
{
	GfxResult
	setLastErrorInfo(GfxResult result, std::string_view title, std::string_view message) noexcept
	{
		return setLastErrorInfo(result, title, message);
	}

	GfxErrorInfo
	generateErrorInfo(GfxResult res, std::string_view title, std::string_view body) noexcept
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
