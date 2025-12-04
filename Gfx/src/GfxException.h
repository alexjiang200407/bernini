#pragma once
#include <Core/except/BerniniException.h>
#include <gfx/common.h>

namespace gfx
{
	class GfxException : public core::except::BerniniException
	{
	public:
		GfxException(
			GfxResult                   result,
			std::string_view                    title = "Graphics Error"sv,
			std::string_view                    body  = "An unknown graphics error occurred."sv,
			std::optional<std::source_location> loc   = std::source_location::current()) noexcept :
			core::except::BerniniException{ title, body, loc }, result{ result }
		{
			lastGraphicsErrorInfo = GenerateErrorInfo(result, title, body);
		}

		virtual ~GfxException() = default;

		GfxResult
		GetErrorResult() const noexcept
		{
			return result;
		}

		static GfxErrorInfo
		GetLastErrorInfo() noexcept
		{
			return lastGraphicsErrorInfo;
		}

		static GfxResult
		SetLastErrorInfo(GfxErrorInfo&& out) noexcept
		{
			lastGraphicsErrorInfo = std::move(out);
			return lastGraphicsErrorInfo.result;
		}

	private:
		static GfxErrorInfo
		GenerateErrorInfo(
			GfxResult res,
			std::string_view  title,
			std::string_view  body) noexcept;

	private:
		GfxResult                               result;
		static inline thread_local GfxErrorInfo lastGraphicsErrorInfo{
			.result = GFX_RESULT_OK,
		};
	};
}
