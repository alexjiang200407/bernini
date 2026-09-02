#include "ui/UiSystemInterface.h"
#include <assetlib/codecs.h>

namespace game
{
	namespace
	{
		constexpr std::string_view c_SchemeSeparator = "://";
	}

	bool
	IsSchemeSource(std::string_view source) noexcept
	{
		const size_t at = source.find(c_SchemeSeparator);

		// Two letters at least, so a drive letter is never one: `C://x` is a path somebody spelled
		// oddly, not a scheme.
		if (at == std::string_view::npos || at < 2)
		{
			return false;
		}

		return std::ranges::all_of(source.substr(0, at), [](const char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
		});
	}

	double
	UiSystemInterface::GetElapsedTime()
	{
		return m_Elapsed;
	}

	bool
	UiSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message)
	{
		switch (type)
		{
		case Rml::Log::LT_ASSERT:
		case Rml::Log::LT_ERROR:
			logger::error("[rmlui] {}", message);
			break;
		case Rml::Log::LT_WARNING:
			logger::warn("[rmlui] {}", message);
			break;
		case Rml::Log::LT_ALWAYS:
		case Rml::Log::LT_INFO:
			logger::info("[rmlui] {}", message);
			break;
		case Rml::Log::LT_DEBUG:
		case Rml::Log::LT_MAX:
		default:
			logger::debug("[rmlui] {}", message);
			break;
		}

		// False would break into the debugger on an assert; the log line and the failed load are
		// what a test reads.
		return true;
	}

	void
	UiSystemInterface::JoinPath(
		Rml::String&       translatedPath,
		const Rml::String& documentPath,
		const Rml::String& path)
	{
		if (IsSchemeSource(path))
		{
			translatedPath = path;
			return;
		}

		// A key is data-root-relative already, so an absolute-looking reference is refused rather
		// than reinterpreted -- there is no host path this could mean.
		const std::string_view directory =
			std::string_view(documentPath).substr(0, documentPath.find_last_of('/') + 1);

		const std::string joined =
			path.starts_with('/') ? path : std::string(directory).append(path);

		try
		{
			std::string normalized = assetlib::normalizePath(joined);
			assetlib::requireInsideDataRoot("game::UiSystemInterface::JoinPath", normalized);
			translatedPath = std::move(normalized);
		}
		catch (const std::exception& e)
		{
			logger::error("UI document '{}' references '{}': {}", documentPath, path, e.what());
			translatedPath.clear();
		}
	}
}
