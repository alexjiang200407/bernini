#pragma once

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
}
