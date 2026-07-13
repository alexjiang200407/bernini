#include "Windows/AssetImporter/AssetImporterDialog.h"

#include "util/QtSupport.h"

#include <QCheckBox>
#include <QLineEdit>

namespace
{
	/** The glTF a test pretends the user dropped. Nothing reads it, so it need not exist. */
	constexpr auto c_SourceFile = "C:/Assets/Exports/stone_wall.glb";
	constexpr auto c_TargetDir  = "C:/Project/Data/Meshes";

	/** The dialog's two checkboxes, in the order it builds them. Neither has an objectName. */
	QCheckBox*
	TexturesBox(const AssetImporterDialog& dialog)
	{
		return dialog.findChildren<QCheckBox*>().value(0);
	}

	QCheckBox*
	AnimationsBox(const AssetImporterDialog& dialog)
	{
		return dialog.findChildren<QCheckBox*>().value(1);
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
