#include "Windows/MaterialEditor/material_overrides.h"

#include "util/QtSupport.h"  // IWYU pragma: keep

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTemporaryDir>

#include <assetlib_structs/BMesh.h>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <qstringliteral.h>
#include <vector>

// The Material combo lists one submesh's looks, and a game reaches one of them by name -- so what
// the panel may register, and where the copy behind it lands, are rules rather than presentation.

namespace
{
	assetlib::BMesh
	MeshWithLooks()
	{
		auto mesh      = assetlib::BMesh();
		mesh.materials = { "Authored/Materials/wood.bmaterial",
			               "Authored/Materials/rust.bmaterial",
			               "Authored/Materials/gold.bmaterial" };

		mesh.materialOverrides = { { 0, "Rusty", 1 },
			                       { 1, "Gilded", 2 },
			                       { 0, "Gone", 7 } };  // a slot the mesh does not have
		return mesh;
	}
}

TEST_CASE("A submesh lists only the looks registered for it", "[materialoverrides]")
{
	const assetlib::BMesh mesh = MeshWithLooks();

	CHECK(
		editor::RegisteredMaterialsFor(mesh, 0) ==
		std::vector<editor::RegisteredMaterial>{
			{ QStringLiteral("Rusty"), QStringLiteral("Authored/Materials/rust.bmaterial") } });
	CHECK(
		editor::RegisteredMaterialsFor(mesh, 1) ==
		std::vector<editor::RegisteredMaterial>{
			{ QStringLiteral("Gilded"), QStringLiteral("Authored/Materials/gold.bmaterial") } });
	CHECK(editor::RegisteredMaterialsFor(mesh, 2).empty());
}

TEST_CASE("A look naming a material the mesh does not carry is not listed", "[materialoverrides]")
{
	// Listing it would put an entry in the combo that resolves to nothing, and selecting it would
	// silently show the submesh's default instead.
	const assetlib::BMesh mesh = MeshWithLooks();
	for (const editor::RegisteredMaterial& look : editor::RegisteredMaterialsFor(mesh, 0))
		CHECK(look.name != QStringLiteral("Gone"));
}

TEST_CASE("A look needs a name of its own", "[materialoverrides]")
{
	const auto taken = std::vector<editor::RegisteredMaterial>{
		{ QStringLiteral("Rusty"), QStringLiteral("Authored/Materials/rust.bmaterial") }
	};

	CHECK(editor::CanRegisterMaterialName(taken, QStringLiteral("Painted")));

	CHECK_FALSE(editor::CanRegisterMaterialName(taken, QString()));
	CHECK_FALSE(editor::CanRegisterMaterialName(taken, QStringLiteral("   ")));

	// One name, two answers: a game asking for "Rusty" could be given either material.
	CHECK_FALSE(editor::CanRegisterMaterialName(taken, QStringLiteral("Rusty")));
	CHECK_FALSE(editor::CanRegisterMaterialName(taken, QStringLiteral("rusty")));

	// The name is registered trimmed, so a stray space is the same look -- and registering it
	// again would silently replace the material behind it.
	CHECK_FALSE(editor::CanRegisterMaterialName(taken, QStringLiteral("Rusty ")));
	CHECK_FALSE(editor::CanRegisterMaterialName(taken, QStringLiteral(" rusty")));
}

TEST_CASE("The look the default names is not listed under it as well", "[materialoverrides]")
{
	QTemporaryDir root;
	REQUIRE(root.isValid());

	const auto dataRoot   = std::filesystem::path(root.path().toStdWString());
	const auto registered = std::vector<editor::RegisteredMaterial>{
		{ QStringLiteral("Rusty"), QStringLiteral("Materials/Rusty.bmaterial") },
		{ QStringLiteral("Painted"), QStringLiteral("Materials/Painted.bmaterial") },
	};

	// Making a look the default does not unregister it -- a game still asks for it by name -- so
	// the default row and its own row would otherwise be the same material twice.
	const std::vector<editor::RegisteredMaterial> listed = editor::LooksBesidesDefault(
		registered,
		QDir(root.path()).filePath(QStringLiteral("Materials/Rusty.bmaterial")),
		dataRoot);

	REQUIRE(listed.size() == 1);
	CHECK(listed[0].name == QStringLiteral("Painted"));
}

TEST_CASE("The look a default gives up is kept, under its own name", "[materialoverrides]")
{
	QTemporaryDir root;
	REQUIRE(root.isValid());

	const auto dataRoot = std::filesystem::path(root.path().toStdWString());
	const auto crate    = QDir(root.path()).filePath(QStringLiteral("Materials/Crate.bmaterial"));

	auto registered =
		std::vector<editor::RegisteredMaterial>{ { QStringLiteral("Rusty"),
		                                           QStringLiteral("Materials/Rusty.bmaterial") } };

	// Without this the material the submesh used to load with leaves the list, and nothing in the
	// project names it any more.
	CHECK(editor::NameForOutgoingDefault(registered, crate, dataRoot) == QStringLiteral("Crate"));

	SECTION("a submesh with no default yet gives up nothing")
	{
		CHECK(editor::NameForOutgoingDefault(registered, QString(), dataRoot).isEmpty());
	}

	SECTION("a default already registered is not registered twice")
	{
		registered.push_back(
			{ QStringLiteral("Original"), QStringLiteral("Materials/Crate.bmaterial") });
		CHECK(editor::NameForOutgoingDefault(registered, crate, dataRoot).isEmpty());
	}

	SECTION("a name already taken is stepped past")
	{
		registered.push_back(
			{ QStringLiteral("Crate"), QStringLiteral("Materials/Other.bmaterial") });
		CHECK(
			editor::NameForOutgoingDefault(registered, crate, dataRoot) ==
			QStringLiteral("Crate 2"));
	}
}

TEST_CASE("A look's copy lands beside the material it varies", "[materialoverrides]")
{
	QTemporaryDir root;
	REQUIRE(root.isValid());

	const auto    dataRoot = std::filesystem::path(root.path().toStdWString());
	const QString from     = QDir(root.path()).filePath(QStringLiteral("Materials/wood.bmaterial"));

	// The look's own name: the directory a mesh's materials sit in already says which Rusty this
	// is, so a stem in front of it would only repeat the folder.
	const QString made = editor::NewOverrideMaterialPath(dataRoot, from, QStringLiteral("Rusty"));

	CHECK(QDir(root.path()).filePath(QStringLiteral("Materials/Rusty.bmaterial")) == made);
}

TEST_CASE("A second look never overwrites the first's material", "[materialoverrides]")
{
	QTemporaryDir root;
	REQUIRE(root.isValid());

	const auto    dataRoot = std::filesystem::path(root.path().toStdWString());
	const QString from     = QDir(root.path()).filePath(QStringLiteral("Materials/wood.bmaterial"));
	REQUIRE(QDir(root.path()).mkpath(QStringLiteral("Materials")));

	const QString first = editor::NewOverrideMaterialPath(dataRoot, from, QStringLiteral("Rusty"));
	REQUIRE(QFile(first).open(QIODevice::WriteOnly));

	// Two submeshes wearing one material, each given a look called Rusty: the second copy must not
	// be written over the first submesh's.
	const QString second = editor::NewOverrideMaterialPath(dataRoot, from, QStringLiteral("Rusty"));

	CHECK(second != first);
	CHECK(second.endsWith(QStringLiteral("Rusty_2.bmaterial")));
}

TEST_CASE("A look copied from an unsaved graph lands under Materials", "[materialoverrides]")
{
	QTemporaryDir root;
	REQUIRE(root.isValid());

	const auto dataRoot = std::filesystem::path(root.path().toStdWString());

	// No file to sit beside, so it lands where a Save As would have offered.
	const QString made =
		editor::NewOverrideMaterialPath(dataRoot, QString(), QStringLiteral(" Rusty "));

	CHECK(made.endsWith(QStringLiteral("Rusty.bmaterial")));
	CHECK(made.startsWith(root.path()));
}
