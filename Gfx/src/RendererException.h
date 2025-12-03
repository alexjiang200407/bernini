#pragma once
#include <Core/except/BerniniException.h>
#include <gfx/Renderer.h>

namespace gfx
{
	class RendererException : public core::except::BerniniException
	{
	public:
		RendererException(
			Bernini_GfxResult                   result,
			std::string_view                    title = "Renderer Error"sv,
			std::string_view                    body  = "An unknown renderer error occurred."sv,
			std::optional<std::source_location> loc   = std::source_location::current()) noexcept :
			core::except::BerniniException{ title, body, loc }, result{ result }
		{
			lastRendererErrorInfo = GenerateErrorInfo(result, title, body);
		}

		virtual ~RendererException() = default;

		Bernini_GfxResult
		GetErrorResult() const noexcept
		{
			return result;
		}

		static Bernini_GfxErrorInfo
		GetLastErrorInfo() noexcept
		{
			return lastRendererErrorInfo;
		}

	private:
		static Bernini_GfxErrorInfo
		GenerateErrorInfo(
			Bernini_GfxResult res,
			std::string_view  title,
			std::string_view  body) noexcept;

	private:
		Bernini_GfxResult                               result;
		static inline thread_local Bernini_GfxErrorInfo lastRendererErrorInfo{
			.result = BERNINI_GFX_RENDERER_RESULT_OK,
		};
	};
}
