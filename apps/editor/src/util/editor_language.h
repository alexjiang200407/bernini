#pragma once

#include <QString>
#include <editor_plugin_api/LanguageResolver.h>
#include <editor_plugin_api/TranslationCatalog.h>
#include <editor_plugin_api/localize.h>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace editor
{
	/**
	 * The one resolver this editor process shows text through: the host's catalogs, then every
	 * plugin's once the window registers them. GUI thread only.
	 *
	 * Reachable by name from `apps/editor/src` alone. A plugin borrows it through
	 * `IEditorHost::GetLanguageResolver` and cannot include this header, so no DLL holds a copy.
	 */
	[[nodiscard]] const LanguageResolver&
	EditorLanguage() noexcept;

	/**
	 * Replaces EditorLanguage() with a resolver in `locale` holding the host's catalogs from
	 * DefaultLocalizationDirectory(), then `plugins`. Every borrower keeps its reference.
	 *
	 * Throws on an unreadable host catalog or a context collision, leaving the previous resolver.
	 */
	void
	InstallEditorLanguage(
		const std::string&                  locale,
		std::span<const TranslationCatalog> plugins = {});

	/** editor::Localize through EditorLanguage(): `key` is `context.name`, `args` fill `{0}`, `{1}`. */
	[[nodiscard]] QString
	Localize(std::string_view key, std::string_view fallback, const TextArgs& args = {});

	/** `localization/` beside the executable, holding the host's own catalogs. */
	[[nodiscard]] std::filesystem::path
	DefaultLocalizationDirectory();

	/** `plugins/bernini.default/localization/` beside the executable: the built-in plugin's catalogs. */
	[[nodiscard]] std::filesystem::path
	BuiltInLocalizationDirectory();

	/** `"locale"` in the editor config, or `en` when the key is absent or the file unreadable. */
	[[nodiscard]] std::string
	ConfiguredLocale(const std::filesystem::path& configPath);

	/**
	 * One catalog per `*.csv` directly in `directory`, whose context is the file's stem, in name
	 * order. None when `directory` does not exist.
	 *
	 * Throws naming the file when one cannot be read or is not a valid translation CSV.
	 */
	[[nodiscard]] std::vector<TranslationCatalog>
	ReadLocalizationDirectory(const std::filesystem::path& directory);
}
