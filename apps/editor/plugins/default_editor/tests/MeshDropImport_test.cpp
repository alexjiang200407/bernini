#include "mesh_drop_import.h"

// <assetlib/codecs.h> carries the AssetCodec<T> specialisations that SaveAt's
// `AssetCodecFor` constraint needs. A template specialisation is not a symbol reference
// include-cleaner can see, so it reads as unused right up until the call stops matching.
#include <assetlib/codecs.h>  // IWYU pragma: keep

#include "StoreAt.h"
#include "util/QtSupport.h"  // IWYU pragma: keep

#include <assetlib/AssetStore.h>
#include <assetlib/import_document.h>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QMimeData>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include <catch2/catch_test_macros.hpp>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/LanguageResolver.h>
#include <exception>
#include <filesystem>
#include <qcontainerfwd.h>
#include <qlist.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// A mesh dropped on a viewport is either already the project's or about to be. Which of the two
// decides whether the user is asked anything at all, so it is a rule rather than presentation.

namespace
{
	/** A host that records what it was asked to import and answers with whatever it was told to. */
	class ImportingHost final : public editor::IEditorHost
	{
	public:
		std::vector<std::filesystem::path> imported;
		std::string                        answer;

		const editor::ILanguageResolver&
		GetLanguageResolver() const noexcept override
		{
			return m_Language;
		}
		const assetlib::AssetStore&
		GetStore() const noexcept override
		{
			std::terminate();
		}
		void
		InvokeRender(const editor::RenderWork&) override
		{
			throw std::runtime_error("Unexpected rendering");
		}
		editor::IEditorViewport*
		CreateViewport(QWidget*, const editor::ViewportDesc&) override
		{
			throw std::runtime_error("Unexpected viewport");
		}
		void
		ShowPanel(std::string_view) override
		{
			throw std::runtime_error("Unexpected panel");
		}
		void
		OpenAsset(std::string_view) override
		{
			throw std::runtime_error("Unexpected asset dispatch");
		}
		std::string
		ImportMeshSource(const std::filesystem::path& source) override
		{
			imported.push_back(source);
			return answer;
		}
		void
		AssetChanged(std::string_view) override
		{
			throw std::runtime_error("Unexpected write");
		}

	private:
		editor::LanguageResolver m_Language;
	};

	void
	SetLocalFiles(QMimeData& mime, const QStringList& paths)
	{
		auto urls = QList<QUrl>();
		for (const QString& path : paths) urls.push_back(QUrl::fromLocalFile(path));

		mime.setUrls(urls);
	}

	/** Writes `Authored/Meshes/<stem>.glb`, and the `.bimport` naming `outputs` beside it. */
	QString
	WriteSource(const QString& dataRoot, const QString& stem, const QStringList& outputs)
	{
		const QDir root(dataRoot);
		REQUIRE(root.mkpath(QStringLiteral("Authored/Meshes")));

		auto document = assetlib::ImportDocument();
		for (const QString& output : outputs) document.outputs.push_back(output.toStdString());

		if (!outputs.isEmpty())
			SaveAt(
				document,
				std::filesystem::path(
					root.filePath(QStringLiteral("Authored/Meshes/") + stem + ".bimport")
						.toStdString()));

		const QString source = root.filePath(QStringLiteral("Authored/Meshes/") + stem + ".glb");
		QFile         glb(source);
		REQUIRE(glb.open(QIODevice::WriteOnly));
		glb.write("source");
		glb.close();

		return source;
	}
}

TEST_CASE("A mesh the project already holds is opened without asking", "[meshdrop]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	ImportingHost host;
	auto          mime = QMimeData();

	SECTION("a dropped container is itself")
	{
		SetLocalFiles(mime, { "/tmp/loose/kirk.bmesh" });
		CHECK(editor::MeshForDrop(host, &mime, root) == QString("/tmp/loose/kirk.bmesh"));
	}

	SECTION("a dropped source resolves through its document")
	{
		const QString source = WriteSource(root, "kirk", { "Derived/Meshes/kirk.bmesh" });
		SetLocalFiles(mime, { source });

		CHECK(
			editor::MeshForDrop(host, &mime, root) ==
			QDir(root).filePath("Derived/Meshes/kirk.bmesh"));
	}

	// The point of both: importing a second copy of what is already here would be the worst
	// possible answer, so an import that resolved is never offered.
	CHECK(host.imported.empty());
}

TEST_CASE("A source this project has never imported is imported first", "[meshdrop]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	ImportingHost host;
	host.answer = "Derived/Meshes/orphan.bmesh";

	const QString orphan = WriteSource(root, "orphan", {});
	auto          mime   = QMimeData();
	SetLocalFiles(mime, { orphan });

	// The answer is a key, and what the viewport opens is a file: joined here, once, rather than
	// by each caller.
	CHECK(
		editor::MeshForDrop(host, &mime, root) ==
		QDir(root).filePath("Derived/Meshes/orphan.bmesh"));
	CHECK(host.imported == std::vector<std::filesystem::path>{ orphan.toStdString() });

	SECTION("and an import that produced no mesh opens nothing")
	{
		// Declined, cancelled, refused over a collision, or imported for its clips alone. The host
		// has already said which, so there is nothing here to add.
		ImportingHost quiet;
		quiet.answer = std::string();

		CHECK(editor::MeshForDrop(quiet, &mime, root).isEmpty());
		CHECK(quiet.imported.size() == 1);
	}
}

TEST_CASE("A source belonging elsewhere is imported rather than reached into", "[meshdrop]")
{
	QTemporaryDir temp;
	QTemporaryDir elsewhere;
	REQUIRE(temp.isValid());
	REQUIRE(elsewhere.isValid());

	// Imported, with a mesh of its own -- but into another project, whose Derived tree this one
	// must not open out of.
	const QString source = WriteSource(elsewhere.path(), "kirk", { "Derived/Meshes/kirk.bmesh" });

	ImportingHost host;
	host.answer = "Derived/Meshes/kirk.bmesh";

	auto mime = QMimeData();
	SetLocalFiles(mime, { source });

	CHECK(
		editor::MeshForDrop(host, &mime, temp.path()) ==
		QDir(temp.path()).filePath("Derived/Meshes/kirk.bmesh"));
	CHECK(host.imported == std::vector<std::filesystem::path>{ source.toStdString() });
}

TEST_CASE("A drag carrying no mesh asks for no import", "[meshdrop]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());

	ImportingHost host;
	auto          mime = QMimeData();

	// A drag of something else entirely is not a failed drop, so nothing is offered and nothing
	// is said.
	SetLocalFiles(mime, { "/tmp/skin.bmaterial" });
	CHECK(editor::MeshForDrop(host, &mime, temp.path()).isEmpty());
	CHECK(editor::MeshForDrop(host, nullptr, temp.path()).isEmpty());
	CHECK(host.imported.empty());
}
