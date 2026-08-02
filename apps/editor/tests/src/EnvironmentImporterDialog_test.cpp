#include "Windows/AssetImporter/EnvironmentImporterDialog.h"

#include <QCheckBox>
#include <QLineEdit>

namespace
{
	/** The HDRI a test pretends the user dropped. Nothing reads it, so it need not exist. */
	constexpr auto c_SourceFile = "C:/Assets/HDRI/forest_4k.hdr";
	constexpr auto c_Project    = "C:/Project/Data";

	QCheckBox*
	SkyBox(const EnvironmentImporterDialog& dialog)
	{
		return dialog.findChild<QCheckBox*>("importSky");
	}

	QCheckBox*
	LightingBox(const EnvironmentImporterDialog& dialog)
	{
		return dialog.findChild<QCheckBox*>("importLighting");
	}

	QCheckBox*
	EnvironmentBox(const EnvironmentImporterDialog& dialog)
	{
		return dialog.findChild<QCheckBox*>("importEnvironment");
	}

	QLineEdit*
	NameField(const EnvironmentImporterDialog& dialog)
	{
		return dialog.findChild<QLineEdit*>("environmentName");
	}
}

// Importing everything is what a dropped HDRI usually means, so it is what the dialog opens on.
TEST_CASE("An environment import offers the whole family by default", "[envimportdialog]")
{
	const EnvironmentImporterDialog dialog(c_SourceFile, c_Project);

	CHECK(dialog.ImportSky());
	CHECK(dialog.ImportLighting());
	CHECK(dialog.ImportEnvironment());

	// Named from the source, so the common case needs nothing typed.
	CHECK(dialog.GetAssetName() == "forest_4k");
}

// A `.benv` composes the other two, so with neither there is nothing for it to name --
// assetlib::importEnvironment refuses that outright, and the dialog must not be able to ask for it.
TEST_CASE("An environment cannot be written without a half to compose", "[envimportdialog]")
{
	const EnvironmentImporterDialog dialog(c_SourceFile, c_Project);

	SkyBox(dialog)->setChecked(false);
	CHECK(dialog.ImportEnvironment());  // the lighting still holds it up

	LightingBox(dialog)->setChecked(false);

	// The box is shown disabled rather than silently ignored, so the refusal is visible before OK.
	CHECK_FALSE(EnvironmentBox(dialog)->isEnabled());
	CHECK_FALSE(dialog.ImportEnvironment());

	SECTION("and it comes back when a half does")
	{
		SkyBox(dialog)->setChecked(true);

		CHECK(EnvironmentBox(dialog)->isEnabled());
		CHECK(dialog.ImportEnvironment());
	}
}

// The halves are separable: a sky is a projection and is done in moments, the lighting is two
// convolutions and is not.
TEST_CASE("Either half of an environment can be imported alone", "[envimportdialog]")
{
	const EnvironmentImporterDialog dialog(c_SourceFile, c_Project);

	SECTION("a sky without its lighting")
	{
		LightingBox(dialog)->setChecked(false);

		CHECK(dialog.ImportSky());
		CHECK_FALSE(dialog.ImportLighting());
	}

	SECTION("a lighting without its sky")
	{
		SkyBox(dialog)->setChecked(false);

		CHECK_FALSE(dialog.ImportSky());
		CHECK(dialog.ImportLighting());
	}
}

// The name is joined onto three category directories and a suffix, so anything that could redirect
// that join would write outside the layout the project guarantees.
TEST_CASE("An environment name that could name another directory is refused", "[envimportdialog]")
{
	const EnvironmentImporterDialog dialog(c_SourceFile, c_Project);

	const QString rejected = GENERATE(
		QString("../escape"),
		QString("sub/dir"),
		QString("sub\\dir"),
		QString("C:/absolute"),
		QString("."),
		QString(".."),
		QString(""),
		QString("   "));

	INFO("typed: " << rejected.toStdString());

	NameField(dialog)->setText(rejected);

	// Falls back to the source's base name rather than refusing the import outright: the user gets a
	// working import under an obvious name instead of an error about punctuation.
	CHECK(dialog.GetAssetName() == "forest_4k");
}

TEST_CASE("A plain environment name is taken as typed", "[envimportdialog]")
{
	const EnvironmentImporterDialog dialog(c_SourceFile, c_Project);

	const QString accepted = GENERATE(
		QString("forest"),
		QString("forest_dusk"),
		QString("forest-2"),
		QString("forest.v2"),
		QString("Forest4K"));

	INFO("typed: " << accepted.toStdString());

	NameField(dialog)->setText(accepted);

	CHECK(dialog.GetAssetName() == accepted);
}

namespace
{
	QLineEdit*
	SkyDirField(const EnvironmentImporterDialog& dialog)
	{
		return dialog.findChild<QLineEdit*>("skyDirectory");
	}

	QLineEdit*
	SourceDirField(const EnvironmentImporterDialog& dialog)
	{
		return dialog.findChild<QLineEdit*>("sourceDirectory");
	}
}

// Each part lands in its category by default: that is the layout Project::Create scaffolds and every
// reference in the project is written against.
TEST_CASE("Each part defaults to its own category", "[envimportdialog]")
{
	const EnvironmentImporterDialog dialog(c_SourceFile, c_Project);

	CHECK(dialog.GetSkyDirectory() == "Sky");
	CHECK(dialog.GetLightingDirectory() == "EnvLighting");
	CHECK(dialog.GetSourceDirectory() == "textures_src");
}

TEST_CASE("A typed folder organises inside its category", "[envimportdialog]")
{
	const EnvironmentImporterDialog dialog(c_SourceFile, c_Project);

	SkyDirField(dialog)->setText("outdoor/dusk");
	SourceDirField(dialog)->setText("hdri");

	CHECK(dialog.GetSkyDirectory() == "Sky/outdoor/dusk");
	CHECK(dialog.GetSourceDirectory() == "textures_src/hdri");

	// The others are untouched by it.
	CHECK(dialog.GetLightingDirectory() == "EnvLighting");
}

// The category is the fixed part. A folder that could re-root the join would put a `.bsky` somewhere
// the project's layout does not cover -- and the content explorer protects the categories, not that.
TEST_CASE("A folder that could leave its category is ignored", "[envimportdialog]")
{
	const EnvironmentImporterDialog dialog(c_SourceFile, c_Project);

	const QString rejected = GENERATE(
		QString("../Meshes"),
		QString("/absolute"),
		QString("C:/elsewhere"),
		QString("D:"),
		QString(".."),
		QString("."),
		QString("   "));

	INFO("typed: " << rejected.toStdString());

	SkyDirField(dialog)->setText(rejected);

	// Falls back to the bare category rather than refusing: the import still lands somewhere correct.
	CHECK(dialog.GetSkyDirectory() == "Sky");
}

// A destination is meaningless when that part is not being written, and leaving it live invites the
// user to fill in a field that does nothing.
TEST_CASE("A part's folder is disabled with the part", "[envimportdialog]")
{
	const EnvironmentImporterDialog dialog(c_SourceFile, c_Project);

	REQUIRE(SkyDirField(dialog)->isEnabled());

	SkyBox(dialog)->setChecked(false);
	CHECK_FALSE(SkyDirField(dialog)->isEnabled());

	SkyBox(dialog)->setChecked(true);
	CHECK(SkyDirField(dialog)->isEnabled());
}
