#include "Windows/AssetImporter/AssetImporterDialog.h"

#include "util/QtSupport.h"
#include <assetlib/bmesh_gltf.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstddef>
#include <qbuffer.h>
#include <string>
#include <vector>

namespace
{
	/** The glTF a test pretends the user dropped. Nothing reads it, so it need not exist. */
	constexpr auto c_SourceFile = "C:/Assets/Exports/stone_wall.glb";

	/** A probe result posing as a file with `pbr` PBR materials out of `total`. */
	std::vector<assetlib::GltfMaterial>
	Probe(size_t total, size_t pbr)
	{
		auto probed = std::vector<assetlib::GltfMaterial>(total);
		for (size_t i = 0; i < total; ++i)
		{
			probed[i].name  = "material" + std::to_string(i);
			probed[i].isPbr = i < pbr;
		}
		return probed;
	}

	QCheckBox*
	TexturesBox(const AssetImporterDialog& dialog)
	{
		return dialog.findChild<QCheckBox*>("importTextures");
	}

	QCheckBox*
	MeshBox(const AssetImporterDialog& dialog)
	{
		return dialog.findChild<QCheckBox*>("importMesh");
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
	Field(const AssetImporterDialog& dialog, const char* objectName)
	{
		return dialog.findChild<QLineEdit*>(objectName);
	}

	/** Whether OK would let the import through, which is what a stated problem takes away. */
	bool
	CanAccept(const AssetImporterDialog& dialog)
	{
		return dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled();
	}
}

TEST_CASE("The importer offers to bring textures across, but not animations", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile);

	// Textures are what a mesh needs to look like anything, so they are on by default.
	REQUIRE(dialog.GetImportTextures());
	REQUIRE(!dialog.GetImportAnimations());

	REQUIRE(TexturesBox(dialog) != nullptr);
	REQUIRE(AnimationsBox(dialog) != nullptr);
}

TEST_CASE("PBR materials come across when the file has some", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(2, 2));

	REQUIRE(MaterialsBox(dialog)->isEnabled());
	REQUIRE(dialog.CanImportPbrMaterials());
}

TEST_CASE("A file with no PBR material cannot import one", "[assetimporter]")
{
	// The offer is refused rather than silently doing nothing: there is no material to derive.
	const auto probe = GENERATE(
		Probe(0, 0),   // no materials at all
		Probe(3, 0));  // materials, but every one of them unlit or spec/gloss

	const AssetImporterDialog dialog(c_SourceFile, probe);

	REQUIRE(!MaterialsBox(dialog)->isEnabled());
	REQUIRE(!dialog.CanImportPbrMaterials());
}

TEST_CASE("Turning textures off takes the materials with them", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(1, 1));

	REQUIRE(dialog.CanImportPbrMaterials());

	// A derived material routes at the extracted texN.ktx2 files. Writing one when those files are not
	// being written would name textures that do not exist -- the reference an import must never make.
	TexturesBox(dialog)->setChecked(false);

	REQUIRE(!MaterialsBox(dialog)->isEnabled());
	REQUIRE(!dialog.CanImportPbrMaterials());

	TexturesBox(dialog)->setChecked(true);

	REQUIRE(dialog.CanImportPbrMaterials());
}

TEST_CASE("An untouched dialog writes where it always did", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(2, 2));

	// Naming the files is an offer, not a change of default: every folder and every name starts at the
	// source's own, so an import nobody touched lands exactly where the folder-only dialog put it.
	const ImportOutputs outputs = dialog.GetOutputs();

	REQUIRE(outputs.mesh == QString("Derived/Meshes/stone_wall/stone_wall.bmesh"));
	REQUIRE(outputs.skeleton == QString("Derived/Skeletons/stone_wall/stone_wall.bskel"));
	REQUIRE(outputs.animations == QString("Derived/Animations/stone_wall/stone_wall.banim"));
	REQUIRE(outputs.materialDir == QString("Authored/Materials/stone_wall"));
	REQUIRE(outputs.textureDir == QString("Derived/SourceTextures/stone_wall"));

	// Index-aligned with the source's material table, which is what the writer iterates.
	REQUIRE(outputs.materialStems == QStringList{ "material0", "material1" });
	REQUIRE(CanAccept(dialog));
}

TEST_CASE("The files stay folded away until they are asked for", "[assetimporter]")
{
	AssetImporterDialog dialog(c_SourceFile, Probe(12, 12));
	dialog.show();
	REQUIRE(editor::test::WaitFor([&] { return dialog.isVisible(); }));

	// Collapsed, the dialog is the folder-per-category one it has always been -- which is what keeps a
	// source carrying twelve materials from opening twelve fields in front of someone who wanted the
	// defaults.
	CHECK(Field(dialog, "materialFolder")->isVisible());
	CHECK_FALSE(Field(dialog, "materialName0")->isVisible());
	CHECK_FALSE(Field(dialog, "meshName")->isVisible());

	dialog.findChild<QToolButton*>("materialFolderToggle")->click();

	// And only the section that was asked for unfolds.
	CHECK(Field(dialog, "materialName0")->isVisible());
	CHECK(Field(dialog, "materialName11")->isVisible());
	CHECK_FALSE(Field(dialog, "meshName")->isVisible());
}

TEST_CASE("A material the import skips is offered no name", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(3, 1));

	// A row for a non-PBR material would promise a file that never appears, so it gets no field -- and
	// the stem list still lines up with the source's table, holding nothing where nothing is written.
	REQUIRE(Field(dialog, "materialName0") != nullptr);
	REQUIRE(Field(dialog, "materialName1") == nullptr);
	REQUIRE(Field(dialog, "materialName2") == nullptr);

	REQUIRE(dialog.GetOutputs().materialStems == QStringList{ "material0", QString(), QString() });
}

// The whole point of a field per category: retyping one must not drag the others along with it.
TEST_CASE("A category's folder moves on its own", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile);

	Field(dialog, "animationFolder")->setText("shared/locomotion");

	const ImportOutputs outputs = dialog.GetOutputs();

	REQUIRE(outputs.animations == QString("Derived/Animations/shared/locomotion/stone_wall.banim"));
	REQUIRE(outputs.mesh == QString("Derived/Meshes/stone_wall/stone_wall.bmesh"));
	REQUIRE(outputs.skeleton == QString("Derived/Skeletons/stone_wall/stone_wall.bskel"));
	REQUIRE(outputs.materialDir == QString("Authored/Materials/stone_wall"));
	REQUIRE(outputs.textureDir == QString("Derived/SourceTextures/stone_wall"));
}

// What the whole change is for: two imports that belong in one folder, told apart by their names
// rather than by a subfolder each.
TEST_CASE("Two imports can share a folder by naming their files apart", "[assetimporter]")
{
	const AssetImporterDialog first(c_SourceFile, Probe(1, 1));
	Field(first, "meshFolder")->setText("animals/coyote");
	Field(first, "meshName")->setText("coyote_skin1");
	Field(first, "materialFolder")->setText("animals/coyote");
	Field(first, "materialName0")->setText("fur_brown");

	const AssetImporterDialog second(c_SourceFile, Probe(1, 1));
	Field(second, "meshFolder")->setText("animals/coyote");
	Field(second, "meshName")->setText("coyote_skin2");
	Field(second, "materialFolder")->setText("animals/coyote");
	Field(second, "materialName0")->setText("fur_grey");

	REQUIRE(first.GetOutputs().mesh == QString("Derived/Meshes/animals/coyote/coyote_skin1.bmesh"));
	REQUIRE(
		second.GetOutputs().mesh == QString("Derived/Meshes/animals/coyote/coyote_skin2.bmesh"));

	REQUIRE(first.GetOutputs().materialDir == second.GetOutputs().materialDir);
	REQUIRE(first.GetOutputs().materialStems == QStringList{ "fur_brown" });
	REQUIRE(second.GetOutputs().materialStems == QStringList{ "fur_grey" });

	REQUIRE(CanAccept(first));
	REQUIRE(CanAccept(second));
}

// A field is a question about where a piece goes, so it stops being one when that piece is not
// coming across.
TEST_CASE("A destination field is dead when its piece is not imported", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(1, 1));

	REQUIRE(Field(dialog, "meshFolder")->isEnabled());
	REQUIRE(Field(dialog, "meshName")->isEnabled());
	REQUIRE(Field(dialog, "skeletonFolder")->isEnabled());
	REQUIRE(Field(dialog, "textureFolder")->isEnabled());
	REQUIRE(Field(dialog, "materialFolder")->isEnabled());
	REQUIRE(!Field(dialog, "animationFolder")->isEnabled());  // animations are off by default

	SECTION("the rig's folder follows the mesh, because the rig rides with it")
	{
		MeshBox(dialog)->setChecked(false);

		REQUIRE(!Field(dialog, "meshFolder")->isEnabled());
		REQUIRE(!Field(dialog, "meshName")->isEnabled());
		REQUIRE(!Field(dialog, "skeletonFolder")->isEnabled());
		REQUIRE(Field(dialog, "textureFolder")->isEnabled());
	}

	SECTION("and the materials' folder follows whether one can be derived at all")
	{
		TexturesBox(dialog)->setChecked(false);

		REQUIRE(!Field(dialog, "materialFolder")->isEnabled());
		REQUIRE(!Field(dialog, "materialName0")->isEnabled());
		REQUIRE(!Field(dialog, "textureFolder")->isEnabled());
	}

	SECTION("a clips-only import still asks where the clips go")
	{
		MeshBox(dialog)->setChecked(false);
		AnimationsBox(dialog)->setChecked(true);

		REQUIRE(Field(dialog, "animationFolder")->isEnabled());
		REQUIRE(Field(dialog, "animationName")->isEnabled());
	}
}

// Unchecking the mesh is how a rig's other animation files come in: the artist exported one file per
// clip, each carrying its own copy of the geometry, and only the clips are wanted.
TEST_CASE("The mesh can be left out of an import", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile);

	REQUIRE(dialog.GetImportMesh());

	MeshBox(dialog)->setChecked(false);
	REQUIRE(!dialog.GetImportMesh());
}

TEST_CASE("A typed destination folder is used", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile);

	SECTION("as typed")
	{
		Field(dialog, "meshFolder")->setText("bricks");

		REQUIRE(dialog.GetOutputs().mesh == QString("Derived/Meshes/bricks/stone_wall.bmesh"));
	}

	SECTION("trimmed")
	{
		Field(dialog, "meshFolder")->setText("  bricks  ");

		REQUIRE(dialog.GetOutputs().mesh == QString("Derived/Meshes/bricks/stone_wall.bmesh"));
	}

	SECTION("nested, because going deeper is fine -- it is going out that is not")
	{
		Field(dialog, "meshFolder")->setText("exterior/walls");

		REQUIRE(
			dialog.GetOutputs().mesh == QString("Derived/Meshes/exterior/walls/stone_wall.bmesh"));
	}
}

TEST_CASE("A destination folder that escapes its category is refused", "[assetimporter]")
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

	const AssetImporterDialog dialog(c_SourceFile);
	Field(dialog, "textureFolder")->setText(typed);

	REQUIRE(dialog.GetOutputs().textureDir == QString("Derived/SourceTextures/stone_wall"));
}

// A folder falls back to the source's name, but a name does not: silently discarding what was
// deliberately typed writes a file the user did not ask for and cannot see coming.
TEST_CASE("A file name that cannot be written stops the import", "[assetimporter]")
{
	const QString typed = GENERATE(
		QString(""),                // nothing at all
		QString("   "),             // nothing, once trimmed
		QString("."),               // names a directory, not a file
		QString(".."),              // names its parent
		QString("walls/stone"),     // a folder separator, which the folder field is for
		QString("..\\..\\system"),  // climbing out, backslashes
		QString("C:/Windows/hal"));

	INFO("typed: " << typed);

	const AssetImporterDialog dialog(c_SourceFile);
	Field(dialog, "meshName")->setText(typed);

	REQUIRE(!dialog.GetProblem().isEmpty());
	REQUIRE(!CanAccept(dialog));

	// And it recovers: the reason goes away with the name that caused it.
	Field(dialog, "meshName")->setText("stone_wall");

	REQUIRE(dialog.GetProblem().isEmpty());
	REQUIRE(CanAccept(dialog));
}

TEST_CASE("Two files of one import cannot be given the same name", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(2, 2));

	REQUIRE(CanAccept(dialog));

	Field(dialog, "materialName1")->setText("material0");

	// Writing both would leave one silently overwritten by the other, which is a loss no later step
	// could report.
	REQUIRE(dialog.GetProblem().contains("material0.bmaterial"));
	REQUIRE(!CanAccept(dialog));
}

TEST_CASE("Two names differing only in case are one file", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(2, 2));

	// On Windows they are the same file, so accepting this on a mac would write a project that loses a
	// material the moment it is opened on the platform the editor ships for.
	Field(dialog, "materialName0")->setText("Rust");
	Field(dialog, "materialName1")->setText("rust");

	REQUIRE(!CanAccept(dialog));
}

TEST_CASE("A name that is dead weight cannot block the import", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(1, 1));

	// Nothing is written through a field whose piece is not coming across, so what it holds is not a
	// reason to refuse the import -- only what will actually land is judged.
	Field(dialog, "animationName")->setText("");

	REQUIRE(!dialog.GetImportAnimations());
	REQUIRE(dialog.GetProblem().isEmpty());
	REQUIRE(CanAccept(dialog));

	AnimationsBox(dialog)->setChecked(true);

	REQUIRE(!dialog.GetProblem().isEmpty());
	REQUIRE(!CanAccept(dialog));
}

TEST_CASE("A name already in the project stops the import as it is typed", "[assetimporter]")
{
	QTemporaryDir root;
	REQUIRE(root.isValid());

	// Exactly the case the change exists for: importing a second skin into a folder the first already
	// occupies. The clash is one file, not the folder, so it must be reported against that file.
	REQUIRE(QDir(root.path()).mkpath("Derived/Meshes/animals/coyote"));

	QFile taken(root.path() + "/Derived/Meshes/animals/coyote/coyote_skin1.bmesh");
	REQUIRE(taken.open(QIODevice::WriteOnly));
	taken.close();

	const AssetImporterDialog dialog(c_SourceFile, {}, root.path());
	Field(dialog, "meshFolder")->setText("animals/coyote");
	Field(dialog, "meshName")->setText("coyote_skin1");

	REQUIRE(dialog.GetProblem().contains("coyote_skin1.bmesh"));
	REQUIRE(!CanAccept(dialog));

	// The folder is not the clash -- the whole point is that another import may already own it.
	Field(dialog, "meshName")->setText("coyote_skin2");

	REQUIRE(dialog.GetProblem().isEmpty());
	REQUIRE(CanAccept(dialog));
}

TEST_CASE("A dialog with no project checks no names against disk", "[assetimporter]")
{
	// The dialog is constructed with the data root, so a caller that has no project on disk -- which is
	// every test above -- must still be able to drive it.
	const AssetImporterDialog dialog(c_SourceFile);

	REQUIRE(dialog.GetProblem().isEmpty());
	REQUIRE(CanAccept(dialog));
}
