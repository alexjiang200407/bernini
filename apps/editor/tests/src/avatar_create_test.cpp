#include "Windows/ContentExplorer/avatar_create.h"
#include "util/source_mesh.h"

#include "StoreAt.h"

#include <assetlib/AssetStore.h>
#include <assetlib/avatar.h>
#include <assetlib/import_document.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

// The Content Explorer's *Create Avatar*, lifted clear of its QMenu: which source offers it, where
// the document lands, and that an avatar somebody already authored is never written over.

namespace
{
	/** A source under Authored/Meshes whose document binds `skeleton` (empty: no rig). */
	QString
	WriteSource(const QString& dataRoot, const QString& stem, const std::string& skeleton)
	{
		const QDir root(dataRoot);
		root.mkpath(QStringLiteral("Authored/Meshes"));

		auto document     = assetlib::ImportDocument();
		document.skeleton = skeleton;
		SaveAt(
			document,
			std::filesystem::path(
				root.filePath(QStringLiteral("Authored/Meshes/") + stem + ".bimport")
					.toStdString()));

		const QString source = root.filePath(QStringLiteral("Authored/Meshes/") + stem + ".glb");
		QFile         glb(source);
		REQUIRE(glb.open(QIODevice::WriteOnly));
		glb.write("source");
		return source;
	}
}

TEST_CASE("A source offers an avatar exactly when its document binds a rig", "[avatar]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString dataRoot = temp.path();

	const QString rigged = WriteSource(dataRoot, "dog", "Derived/Skeletons/dog.bskel");
	const QString flat   = WriteSource(dataRoot, "crate", "");

	CHECK(editor::GetSourceSkeleton(dataRoot, rigged) == "Derived/Skeletons/dog.bskel");
	CHECK(editor::GetSourceSkeleton(dataRoot, flat).isEmpty());

	// Not a source at all, and a source with no document behind it.
	CHECK(
		editor::GetSourceSkeleton(dataRoot, QDir(dataRoot).filePath("Authored/Meshes/dog.bimport"))
			.isEmpty());
	CHECK(
		editor::GetSourceSkeleton(dataRoot, QDir(dataRoot).filePath("Authored/Meshes/lost.glb"))
			.isEmpty());
}

TEST_CASE("Create Avatar writes the empty document where the rig's convention finds it", "[avatar]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString dataRoot = temp.path();

	const QString key = editor::CreateEmptyAvatar(dataRoot, "Derived/Skeletons/dog.bskel");

	// At the key avatarKeyFor names, and nowhere the action had to spell itself: the path *is*
	// the attachment, and a document one folder off would belong to no rig.
	CHECK(key == QString::fromStdString(assetlib::avatarKeyFor("Derived/Skeletons/dog.bskel")));
	CHECK(key == "Authored/Skeletons/dog.bavatar");

	const assetlib::Avatar written =
		assetlib::AssetStore(std::filesystem::path(dataRoot.toStdWString()))
			.Load<assetlib::Avatar>(key.toStdString());
	CHECK(written.legs.empty());

	SECTION("a second Create Avatar is refused rather than emptying what was authored")
	{
		auto authored = assetlib::Avatar();
		authored.legs.push_back({ "Dog L Thigh", "Dog L Calf", "Dog L Foot", "Dog L Toe" });
		assetlib::AssetStore(std::filesystem::path(dataRoot.toStdWString()))
			.Save(authored, key.toStdString());

		CHECK_THROWS(editor::CreateEmptyAvatar(dataRoot, "Derived/Skeletons/dog.bskel"));

		// And the legs are still there.
		CHECK(
			assetlib::AssetStore(std::filesystem::path(dataRoot.toStdWString()))
				.Load<assetlib::Avatar>(key.toStdString()) == authored);
	}

	SECTION("a key that is not a skeleton's is refused")
	{
		CHECK_THROWS(editor::CreateEmptyAvatar(dataRoot, "Derived/Meshes/dog.bmesh"));
	}
}
