#include "Async/BackgroundTask.h"
#include "Windows/MaterialEditor/material_io.h"
#include "test_editor_graph.h"

#include <QTemporaryDir>

#include <assetlib/AssetStore.h>
#include <assetlib_structs/BMaterial.h>
#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>
#include <filesystem>
#include <qcontainerfwd.h>
#include <qstring.h>
#include <qstringliteral.h>
#include <qtypes.h>
#include <string>
#include <string_view>

// Bake All reads every material off disk and saves each back. A material with no node graph is one
// no save may write, so it is found before anything is baked rather than midway through the batch.

namespace
{
	constexpr std::string_view c_Authored  = "Authored/Materials/authored.bmaterial";
	constexpr std::string_view c_Graphless = "Authored/Materials/handwritten.bmaterial";

	/** A data root holding one authored material and one written without a graph. */
	struct Batch
	{
		QTemporaryDir temp;

		Batch()
		{
			auto material        = assetlib::BMaterial();
			material.name        = "authored";
			material.editorGraph = std::string(editor::test::c_TestEditorGraph);
			assetlib::AssetStore(Root()).Save(material, c_Authored);

			core::file::write_atomic(
				Root() / c_Graphless,
				std::string_view(R"({ "name": "handwritten", "shadingModel": "pbr" })"));
		}

		[[nodiscard]] std::filesystem::path
		Root() const
		{
			return std::filesystem::path(temp.path().toStdWString());
		}
	};

	background::TaskResult
	Bake(const Batch& batch, const QStringList& materials)
	{
		return background::RunReporting(
			[](int, int, const QString&) {},
			[&](background::Progress& progress) {
				editor::BakeMaterials(batch.Root(), materials, progress);
			});
	}
}

TEST_CASE("Bake All names every material with no graph and bakes none", "[materialeditor][graph]")
{
	const Batch batch;
	const auto  before = core::file::read_file_bytes(batch.Root() / c_Authored);

	// The authored one comes first: a run that found the other only when saving it would already
	// have rewritten this one.
	const background::TaskResult result = Bake(
		batch,
		{ QString::fromUtf8(c_Authored.data(), static_cast<qsizetype>(c_Authored.size())),
	      QString::fromUtf8(c_Graphless.data(), static_cast<qsizetype>(c_Graphless.size())) });

	REQUIRE(result.status == background::TaskStatus::kFailed);
	CHECK(result.error.contains(QStringLiteral("nothing was baked")));
	CHECK(result.error.contains(QStringLiteral("handwritten.bmaterial")));
	CHECK_FALSE(result.error.contains(QStringLiteral("authored.bmaterial")));

	CHECK(core::file::read_file_bytes(batch.Root() / c_Authored) == before);
}

TEST_CASE("Bake All over authored materials completes", "[materialeditor][graph]")
{
	const Batch batch;

	const background::TaskResult result = Bake(
		batch,
		{ QString::fromUtf8(c_Authored.data(), static_cast<qsizetype>(c_Authored.size())) });

	CHECK(result.Completed());
}
