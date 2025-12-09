#pragma once

namespace core::except
{
	class BerniniException : public std::exception
	{
	public:
		BerniniException(
			std::string_view                    title = "Unknown Error"sv,
			std::string_view                    body  = "No additional information."sv,
			std::optional<std::source_location> loc   = std::source_location::current()) noexcept :
			title{ title }, body{ body }, loc{ loc }
		{
			formatted = std::format("{}\n{}", this->title, this->body);
		}

		std::string_view
		Title() const noexcept
		{
			return title;
		}

		std::string_view
		Body() const noexcept
		{
			return body;
		}

		std::optional<std::source_location>
		SourceLocation() const noexcept
		{
			return loc;
		}

		const char*
		what() const noexcept override
		{
			return formatted.c_str();
		}

	protected:
		std::string                         title;
		std::string                         body;
		std::string                         formatted;
		std::optional<std::source_location> loc;
	};
}
