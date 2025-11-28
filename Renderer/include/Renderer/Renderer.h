#pragma once

#ifdef _WIN32
#	ifdef RENDERER_EXPORTS
#		define RENDERER_API __declspec(dllexport)
#	else
#		define RENDERER_API __declspec(dllimport)
#	endif
#else
#	define RENDERER_API
#endif

namespace renderer
{
	class RendererException : public std::exception
	{
	public:
		explicit RendererException(
			const std::string&   cause,
			std::source_location loc = std::source_location::current()) noexcept;

		virtual ~RendererException() = default;

		virtual std::string_view
		GetType() const noexcept
		{
			return "Unknown Type"sv;
		}

		const char*
		what() const override
		{
			return message.c_str();
		}

	private:
		std::string          cause;
		std::string          message;
		std::source_location loc;
	};

	struct RendererOptions
	{
		int width  = 0;
		int height = 0;
	};

	class IRenderer
	{
	public:
		virtual ~IRenderer() = default;

		virtual void
		StartFrame() = 0;

		static std::unique_ptr<IRenderer>
		Create(const RendererOptions& options = {});
	};

}
