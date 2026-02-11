#pragma once
#include <core/except/BerniniException.h>
#include <gfx/ffi/common.h>

#ifdef _DEBUG
#	define THROW_GFX_ERROR(title, message) throw core::except::BerniniException(title, message)
#else
#	define THROW_GFX_ERROR(title, message)          \
		{                                            \
			logger::error("{}: {}", title, message); \
			std::abort();                            \
		}
#endif

namespace gfx
{
	static inline thread_local GfxErrorInfo lastGraphicsErrorInfo{
		.result = GFX_RESULT_OK,
	};

	GfxErrorInfo
	generateErrorInfo(GfxResult res, std::string_view title, std::string_view body) noexcept;

	inline GfxErrorInfo
	getLastErrorInfo() noexcept
	{
		return lastGraphicsErrorInfo;
	}

	GfxResult
	setLastErrorInfo(GfxResult result, std ::string_view title, std::string_view message) noexcept;

	inline GfxResult
	getLastResult() noexcept
	{
		return lastGraphicsErrorInfo.result;
	}
}
