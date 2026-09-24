#include "util/editor_language.h"

#include "Plugins/plugin_loader.h"
#include <QString>
#include <algorithm>
#include <core/err/util.h>
#include <core/file/file.h>
#include <core/settings/Settings.h>
#include <cstddef>
#include <editor_plugin_api/LanguageResolver.h>
#include <editor_plugin_api/TranslationCatalog.h>
#include <editor_plugin_api/localize.h>
#include <editor_plugin_api/translation_csv.h>
#include <exception>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor
{
	namespace
	{
		LanguageResolver&
		Language() noexcept
		{
			static LanguageResolver g_Language;
			return g_Language;
		}
	}

	const LanguageResolver&
	EditorLanguage() noexcept
	{
		return Language();
	}

	void
	InstallEditorLanguage(
		const std::string&                        locale,
		const std::span<const TranslationCatalog> plugins)
	{
		LanguageResolver language;
		language.SetLocale(locale);
		for (const TranslationCatalog& catalog :
		     ReadLocalizationDirectory(DefaultLocalizationDirectory()))
			language.RegisterCatalog(catalog);
		for (const TranslationCatalog& catalog : plugins) language.RegisterCatalog(catalog);
		Language() = std::move(language);
	}

	QString
	Localize(const std::string_view key, const TextArgs& args, const std::string_view fallback)
	{
		return Localize(EditorLanguage(), key, args, fallback);
	}

	QString
	Localize(const std::string_view key, const std::string_view fallback)
	{
		return Localize(EditorLanguage(), key, fallback);
	}

	LocalizedError::LocalizedError(const std::string_view key, const std::string_view fallback) :
		LocalizedError(key, TextArgs(), fallback)
	{}

	// The English is the fallback, formatted by a resolver with no catalog: the lint holds it equal
	// to the `en` row.
	LocalizedError::LocalizedError(
		const std::string_view key,
		const TextArgs&        args,
		const std::string_view fallback) :
		std::runtime_error(editor::Localize(LanguageResolver(), key, args, fallback).toStdString()),
		m_Shown(Localize(key, args, fallback))
	{}

	const QString&
	LocalizedError::Shown() const noexcept
	{
		return m_Shown;
	}

	QString
	ShownText(const std::exception& e)
	{
		if (const auto* localized = dynamic_cast<const LocalizedError*>(&e))
			return localized->Shown();
		return QString::fromUtf8(e.what());
	}

	std::filesystem::path
	DefaultLocalizationDirectory()
	{
		return core::file::get_executable_path().parent_path() / "localization";
	}

	std::filesystem::path
	BuiltInLocalizationDirectory()
	{
		return plugins::DefaultPluginRoot() / plugins::c_BuiltInPluginId / "localization";
	}

	std::string
	ConfiguredLocale(const std::filesystem::path& configPath)
	{
		try
		{
			return core::Settings(configPath)["locale"].GetOrDefault(std::string("en"));
		}
		catch (const std::exception&)
		{
			return "en";
		}
	}

	std::vector<TranslationCatalog>
	ReadLocalizationDirectory(const std::filesystem::path& directory)
	{
		std::vector<std::filesystem::path> files;
		if (std::filesystem::is_directory(directory))
			for (const std::filesystem::directory_entry& entry :
			     std::filesystem::directory_iterator(directory))
				if (entry.is_regular_file() && entry.path().extension() == ".csv")
					files.push_back(entry.path());
		std::ranges::sort(files);

		std::vector<TranslationCatalog> catalogs;
		for (const std::filesystem::path& file : files)
		{
			try
			{
				const std::vector<std::byte> bytes = core::file::read_file_bytes(file);
				catalogs.push_back(ReadTranslationCsv(
					file.stem().string(),
					std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
			}
			catch (const std::exception& e)
			{
				core::throw_runtime_error("{}: {}", file.string(), e.what());
			}
		}
		return catalogs;
	}
}
