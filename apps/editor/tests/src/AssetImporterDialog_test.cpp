#include "Windows/AssetImporter/AssetImporterDialog.h"

#include "util/QtSupport.h"

#include <QCheckBox>
#include <QLineEdit>

namespace
{
	/** The glTF a test pretends the user dropped. Nothing reads it, so it need not exist. */
	constexpr auto c_SourceFile = "C:/Assets/Exports/stone_wall.glb";
	constexpr auto c_TargetDir  = "C:/Project/Data/Meshes";

	/** A probe result posing as a file with `pbr` PBR materials out of `total`. */
	assetlib::GltfMaterialProbe
	Probe(size_t total, size_t pbr)
	{
		return assetlib::GltfMaterialProbe{ total, pbr };
	}

	QCheckBox*
	TexturesBox(const AssetImporterDialog& dialog)
	{
		return dialog.findChild<QCheckBox*>("importTextures");
	}

	QCheckBox*
	MaterialsBox(const AssetImporterDialog& dialog)
	{
		return dialog.findChild<QCheckBox*>("importPbrMaterials");
	}

	QCheckBox*
	AnimationsBox(const AssetImporterDialog& dialog)
	{
		return dialog.findChild<QCheckBox*>("importAnimations");
	}

	QLineEdit*
	SubdirField(const AssetImporterDialog& dialog)
	{
		return dialog.findChild<QLineEdit*>();
	}
}

TEST_CASE("The importer offers to bring textures across, but not animations", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, c_TargetDir);

	// Textures are what a mesh needs to look like anything, so they are on by default.
	REQUIRE(dialog.ImportTextures());
	REQUIRE(!dialog.ImportAnimations());

	REQUIRE(TexturesBox(dialog) != nullptr);
	REQUIRE(AnimationsBox(dialog) != nullptr);
}

TEST_CASE("PBR materials come across when the file has some", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, c_TargetDir, Probe(2, 2));

	REQUIRE(MaterialsBox(dialog)->isEnabled());
	REQUIRE(dialog.CanImportPbrMaterials());
}

TEST_CASE("A file with no PBR material cannot import one", "[assetimporter]")
{
	// The offer is refused rather than silently doing nothing: there is no material to derive.
	const auto probe = GENERATE(
		Probe(0, 0),   // no materials at all
		Probe(3, 0));  // materials, but every one of them unlit or spec/gloss

	const AssetImporterDialog dialog(c_SourceFile, c_TargetDir, probe);

	REQUIRE(!MaterialsBox(dialog)->isEnabled());
	REQUIRE(!dialog.CanImportPbrMaterials());
}

TEST_CASE("Turning textures off takes the materials with them", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, c_TargetDir, Probe(1, 1));

	REQUIRE(dialog.CanImportPbrMaterials());

	// A derived material routes at the extracted texN.ktx2 files. Writing one when those files are not
	// being written would name textures that do not exist -- the reference an import must never make.
	TexturesBox(dialog)->setChecked(false);

	REQUIRE(!MaterialsBox(dialog)->isEnabled());
	REQUIRE(!dialog.CanImportPbrMaterials());

	TexturesBox(dialog)->setChecked(true);

	REQUIRE(dialog.CanImportPbrMaterials());
}

TEST_CASE("The texture folder defaults to the file's name", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, c_TargetDir);

	// Every import needs its own folder -- the extracted files are named tex0.ktx2, tex1.ktx2 by
	// index, so two imports sharing one would overwrite each other. Naming it after the source is the
	// default that makes that collision unlikely.
	REQUIRE(dialog.TextureSubdirectory() == QString("stone_wall"));
}

TEST_CASE("Turning textures off disables the folder to put them in", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, c_TargetDir);

	REQUIRE(SubdirField(dialog)->isEnabled());

	// A destination for textures that are not being extracted is not a question worth asking.
	TexturesBox(dialog)->setChecked(false);

	REQUIRE(!SubdirField(dialog)->isEnabled());
	REQUIRE(!dialog.ImportTextures());
}

TEST_CASE("A typed texture folder is used", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, c_TargetDir);

	SECTION("as typed")
	{
		SubdirField(dialog)->setText("bricks");

		REQUIRE(dialog.TextureSubdirectory() == QString("bricks"));
	}

	SECTION("trimmed")
	{
		SubdirField(dialog)->setText("  bricks  ");

		REQUIRE(dialog.TextureSubdirectory() == QString("bricks"));
	}

	SECTION("nested, because going deeper is fine -- it is going out that is not")
	{
		SubdirField(dialog)->setText("exterior/walls");

		REQUIRE(dialog.TextureSubdirectory() == QString("exterior/walls"));
	}
}

TEST_CASE("A texture folder that escapes the project is refused", "[assetimporter]")
{
	// Every one of these would put the import's textures somewhere other than inside the project's
	// texture root. The dialog falls back to the default rather than honouring any of it.
	const QString typed = GENERATE(
		QString(""),                        // nothing at all
		QString("   "),                     // nothing, once trimmed
		QString("."),                       // the texture root itself
		QString(".."),                      // its parent
		QString("../../Windows"),           // climbing out
		QString("walls/../../../Windows"),  // climbing out the long way round
		QString("C:/Windows/System32"),     // absolute
		QString("/etc"),                    // absolute, posix style
		QString("\\Windows"),               // rooted on the current drive
		// A drive-relative path carries a root name, and std::filesystem's join replaces the left side
		// when the right has one -- so this would silently re-root the whole path off the project.
		QString("D:walls"),
		QString("D:"));

	INFO("typed: " << typed);

	const AssetImporterDialog dialog(c_SourceFile, c_TargetDir);
	SubdirField(dialog)->setText(typed);

	REQUIRE(dialog.TextureSubdirectory() == QString("stone_wall"));
}
