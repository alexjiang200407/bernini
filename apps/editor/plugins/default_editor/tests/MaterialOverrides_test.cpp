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

TEST_CASE("A look's copy lands beside the material it varies", "[materialoverrides]")
{
	QTemporaryDir root;
	REQUIRE(root.isValid());

	const auto    dataRoot = std::filesystem::path(root.path().toStdWString());
	const QString from     = QDir(root.path()).filePath(QStringLiteral("Materials/wood.bmaterial"));

	const QString made = editor::NewOverrideMaterialPath(
		dataRoot,
		from,
		QStringLiteral("crate[0]"),
		QStringLiteral("Rusty"));

	CHECK(QDir(root.path()).filePath(QStringLiteral("Materials/wood_Rusty.bmaterial")) == made);
}

TEST_CASE("A second look never overwrites the first's material", "[materialoverrides]")
{
	QTemporaryDir root;
	REQUIRE(root.isValid());

	const auto    dataRoot = std::filesystem::path(root.path().toStdWString());
	const QString from     = QDir(root.path()).filePath(QStringLiteral("Materials/wood.bmaterial"));
	REQUIRE(QDir(root.path()).mkpath(QStringLiteral("Materials")));

	const QString first = editor::NewOverrideMaterialPath(
		dataRoot,
		from,
		QStringLiteral("crate[0]"),
		QStringLiteral("Rusty"));
	REQUIRE(QFile(first).open(QIODevice::WriteOnly));

	// Two submeshes wearing one material, each given a look called Rusty: the second copy must not
	// be written over the first submesh's.
	const QString second = editor::NewOverrideMaterialPath(
		dataRoot,
		from,
		QStringLiteral("crate[1]"),
		QStringLiteral("Rusty"));

	CHECK(second != first);
	CHECK(second.endsWith(QStringLiteral("wood_Rusty_2.bmaterial")));
}

TEST_CASE("A look copied from an unsaved graph is named from the submesh", "[materialoverrides]")
{
	QTemporaryDir root;
	REQUIRE(root.isValid());

	const auto dataRoot = std::filesystem::path(root.path().toStdWString());

	// No file to sit beside, so it lands where a Save As would have offered.
	const QString made = editor::NewOverrideMaterialPath(
		dataRoot,
		QString(),
		QStringLiteral("crate[0]"),
		QStringLiteral(" Rusty "));

	CHECK(made.endsWith(QStringLiteral("crate[0]_Rusty.bmaterial")));
	CHECK(made.startsWith(root.path()));
}
