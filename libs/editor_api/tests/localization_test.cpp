#include <QByteArray>
#include <QString>
#include <catch2/catch_test_macros.hpp>
#include <editor_api/LanguageResolver.h>
#include <editor_api/LocalizedText.h>
#include <editor_api/TranslationCatalog.h>
#include <editor_api/translation_csv.h>
#include <string>

TEST_CASE("Hosts own independent languages and copied module catalogs", "[plugin][localization]")
{
	editor::LanguageResolver    editorHost;
	editor::LanguageResolver    gameHost;
	const editor::LocalizedText label{ "sample.editor", "open_file", "Open file" };
	{
		auto catalog = editor::ReadTranslationCsv(
			"sample.editor",
			"key,en,zh_CN\nopen_file,Open,打开文件\nsave_file,Save,\n");
		editorHost.RegisterCatalog(catalog);
		gameHost.RegisterCatalog(catalog);
		catalog.entries.front().text = "Changed by plugin";
	}
	gameHost.SetLocale("zh_CN");
	REQUIRE(label.Resolve(editorHost) == "Open");
	REQUIRE(label.Resolve(gameHost) == QString::fromUtf8("打开文件"));
	const editor::LocalizedText missing{ "sample.editor", "save_file", "Save file" };
	REQUIRE(missing.Resolve(gameHost) == "Save file");
	gameHost.SetLocale("fr");
	REQUIRE(label.Resolve(gameHost) == "Open file");
	REQUIRE(label.Resolve(editorHost) == "Open");
	REQUIRE_THROWS(editorHost.SetLocale(""));
	REQUIRE(label.Resolve(editorHost) == "Open");
}

TEST_CASE(
	"Catalog rejection preserves earlier state and permits a corrected retry",
	"[plugin][localization]")
{
	editor::LanguageResolver         resolver;
	const editor::LocalizedText      label{ "sample.editor", "open_file", "Fallback" };
	const editor::TranslationCatalog valid{ "sample.editor", { { "open_file", "en", "Open" } } };
	for (const auto& invalid :
	     { editor::TranslationCatalog{
			   "sample.editor",
			   { { "open_file", "en", "Open" }, { "Bad Key", "en", "Bad" } } },
	       editor::TranslationCatalog{
			   "sample.editor",
			   { { "open_file", "en", "Open" }, { "open_file", "en", "Duplicate" } } },
	       editor::TranslationCatalog{ "sample.editor", { { "open_file", "", "Open" } } },
	       editor::TranslationCatalog{ "sample.editor", { { "open_file", "en", "" } } },
	       editor::TranslationCatalog{ "sample..editor", {} } })
	{
		REQUIRE_THROWS(resolver.RegisterCatalog(invalid));
		REQUIRE(label.Resolve(resolver) == "Fallback");
	}
	resolver.RegisterCatalog(valid);
	REQUIRE(label.Resolve(resolver) == "Open");
	REQUIRE_THROWS(
		resolver.RegisterCatalog({ "sample.editor", { { "open_file", "en", "Overwrite" } } }));
	REQUIRE(label.Resolve(resolver) == "Open");
	resolver.RegisterCatalog({ "other.editor", { { "open_file", "en", "Other" } } });
	REQUIRE(label.Resolve(resolver) == "Open");
}

TEST_CASE(
	"Translation CSV preserves quoted Unicode text and placeholders",
	"[plugin][localization]")
{
	editor::LanguageResolver resolver;
	resolver.RegisterCatalog(
		editor::ReadTranslationCsv(
			"sample.editor",
			"\xEF\xBB\xBFkey,en,zh_CN\r\nopen_file,\"Open, \"\"{name}\"\"\nnow\",打开文件\r\n"));
	const editor::LocalizedText label{ "sample.editor", "open_file", "Fallback" };
	REQUIRE(label.Resolve(resolver) == "Open, \"{name}\"\nnow");
	resolver.SetLocale("zh_CN");
	REQUIRE(label.Resolve(resolver) == QString::fromUtf8("打开文件"));
	const auto csv = editor::ReadTranslationCsv("sample.editor", "key,en\nopen_file,Open");
	REQUIRE(csv.entries.size() == 1);
	REQUIRE(csv.entries.front().text == "Open");
}

TEST_CASE(
	"Malformed translation CSV is rejected before catalog registration",
	"[plugin][localization]")
{
	for (const auto* csv : { "",
	                         "key\n",
	                         "context,en\n",
	                         "key,\n",
	                         "key,en,en\n",
	                         "key,en\nopen_file,Open,Extra\n",
	                         "key,en\nopen_file\n",
	                         "key,en\nOpen File,Open\n",
	                         "key,en\n1open,Open\n",
	                         "key,en\nopen_file,Open\nopen_file,Again\n",
	                         "key,en\nopen_file,\"Unclosed",
	                         "key,en\nopen_file,Un\"quoted\n",
	                         "key,en\nopen_file,\"Closed\"junk\n",
	                         "key,en\ropen_file,Open",
	                         "key,en\nopen_file,\xFF\n",
	                         "key,en\n\n" })
		REQUIRE_THROWS(editor::ReadTranslationCsv("sample.editor", csv));
	const std::string nulCsv("key,en\nopen_file,A\0B", 20);
	REQUIRE_THROWS(editor::ReadTranslationCsv("sample.editor", nulCsv));
	REQUIRE_THROWS(editor::ReadTranslationCsv("bad context", "key,en\n"));
}
