#include "Windows/MaterialEditor/MaterialEditorWindow.h"

#include "util/QtSupport.h"

#include <QDir>
#include <QTemporaryDir>

#include <assetlib_structs/BMaterial.h>

// Set Default Material writes the material into the `.bmesh`. Doing that when the mesh already names
// it rewrites the file to say what it already says, so the button greys out -- which turns on telling
// "the same file" from "a different one", and the two paths being compared reach the window by
// different routes: one from a file dialog, one from the mesh's own relative path resolved against the
// data root. They can spell the same file differently, and a string compare would call that a
// difference.
//
// The window itself cannot be driven here: without a graphics device it has no preview, so it has no
// submesh graphs and the button never has a material to act on. This pins the rule the button asks.
//
// Note QFileInfo's own comparison cannot answer this: it falls back to canonicalFilePath(), which is
// empty for a file that does not exist, so two *different* missing paths come back equal.

TEST_CASE("An unbound submesh is never already default", "[materialeditor]")
{
	CHECK_FALSE(
		MaterialEditorWindow::IsAlreadyDefault(QString(), "C:/Data/Materials/Leaf.bmaterial"));
}

TEST_CASE("An unsaved graph is never already default", "[materialeditor]")
{
	// Nothing on disk to bind: Save first. Enabling the button here would bind a path to nothing.
	CHECK_FALSE(
		MaterialEditorWindow::IsAlreadyDefault("C:/Data/Materials/Leaf.bmaterial", QString()));
}

TEST_CASE("The material the mesh already names is already default", "[materialeditor]")
{
	CHECK(
		MaterialEditorWindow::IsAlreadyDefault(
			"C:/Data/Materials/Leaf.bmaterial",
			"C:/Data/Materials/Leaf.bmaterial"));
}

TEST_CASE("A different material is not already default", "[materialeditor]")
{
	CHECK_FALSE(
		MaterialEditorWindow::IsAlreadyDefault(
			"C:/Data/Materials/Leaf.bmaterial",
			"C:/Data/Materials/Wood.bmaterial"));
}

TEST_CASE("The same file spelled differently is still already default", "[materialeditor]")
{
	// The .bmesh's path is resolved with std::filesystem (native separators); a file dialog hands back
	// forward slashes. Same file.
	CHECK(
		MaterialEditorWindow::IsAlreadyDefault(
			"C:\\Data\\Materials\\Leaf.bmaterial",
			"C:/Data/Materials/Leaf.bmaterial"));

	// A data root that is not already normalised resolves through a parent segment.
	CHECK(
		MaterialEditorWindow::IsAlreadyDefault(
			"C:/Data/Textures/../Materials/Leaf.bmaterial",
			"C:/Data/Materials/Leaf.bmaterial"));
}

TEST_CASE("A real file reached two ways is already default", "[materialeditor]")
{
	// The spellings above are compared without touching the disk. This one exists, so QFileInfo can
	// resolve both to the same entry -- including the case-insensitivity of the filesystem underneath,
	// which is what a string compare would get wrong on Windows.
	QTemporaryDir dir;
	REQUIRE(dir.isValid());

	const QString path = QDir(dir.path()).filePath("Leaf.bmaterial");
	{
		QFile file(path);
		REQUIRE(file.open(QIODevice::WriteOnly));
		file.write("bmaterial");
	}

	const QString viaParent = QDir(dir.path()).filePath("./Leaf.bmaterial");

	CHECK(MaterialEditorWindow::IsAlreadyDefault(path, viaParent));
	CHECK_FALSE(
		MaterialEditorWindow::IsAlreadyDefault(path, QDir(dir.path()).filePath("Other.bmaterial")));
}

TEST_CASE("Two materials that do not exist are still told apart", "[materialeditor]")
{
	// A material can be deleted out from under a mesh that still names it. If the two compared equal
	// merely by both being absent, Set Default Material would grey out on every mesh.
	CHECK_FALSE(
		MaterialEditorWindow::IsAlreadyDefault(
			"C:/Nowhere/Leaf.bmaterial",
			"C:/Nowhere/Wood.bmaterial"));
}

TEST_CASE("Case is not what tells two materials apart", "[materialeditor]")
{
	// Windows: the .bmesh's path comes back from std::filesystem, a file dialog's from the shell, and
	// they need not agree on case.
	CHECK(
		MaterialEditorWindow::IsAlreadyDefault(
			"C:/Data/Materials/Leaf.bmaterial",
			"C:/data/materials/leaf.bmaterial"));
}

TEST_CASE("A baked material lists the textures it names", "[materialeditor]")
{
	// "Show the current baked textures if any": the paths the material's last bake wrote, one per line,
	// so the artist can see what the mesh actually samples without opening the files.
	auto material                 = assetlib::BMaterial();
	material.shadingModel         = assetlib::ShadingModel::kPbr;
	material.mode                 = assetlib::MaterialMode::kBaked;
	material.pbr.baseColorTexture = "Textures/basecolor_a1b2.ktx2";
	material.pbr.normalTexture    = "Textures/normal_c3d4.ktx2";
	material.pbr.ormTexture       = "Textures/orm_e5f6.ktx2";

	const QString summary = MaterialEditorWindow::BakedTexturesSummary(material);

	CHECK(summary.contains("Textures/basecolor_a1b2.ktx2"));
	CHECK(summary.contains("Textures/normal_c3d4.ktx2"));
	CHECK(summary.contains("Textures/orm_e5f6.ktx2"));
}

TEST_CASE("A material with no baked triplet lists nothing", "[materialeditor]")
{
	// A material authored but never baked carries only routes, no triplet -- there is nothing baked to
	// show, and the empty string is what keeps the label hidden.
	auto material                  = assetlib::BMaterial();
	material.shadingModel          = assetlib::ShadingModel::kPbr;
	material.mode                  = assetlib::MaterialMode::kLoose;
	material.pbr.routes[0].texture = "textures_src/albedo.ktx2";  // a source route, not a baked map

	CHECK(MaterialEditorWindow::BakedTexturesSummary(material).isEmpty());
}

TEST_CASE(
	"A material baked without every map shows a dash for the one it lacks",
	"[materialeditor]")
{
	// Base colour and ORM baked, no normal routed: the missing map reads as a dash rather than a blank
	// that looks like a bug, and the listing still shows because something is baked.
	auto material                 = assetlib::BMaterial();
	material.shadingModel         = assetlib::ShadingModel::kPbr;
	material.mode                 = assetlib::MaterialMode::kBaked;
	material.pbr.baseColorTexture = "Textures/basecolor_a1b2.ktx2";
	material.pbr.ormTexture       = "Textures/orm_e5f6.ktx2";

	const QString summary = MaterialEditorWindow::BakedTexturesSummary(material);

	REQUIRE_FALSE(summary.isEmpty());
	CHECK(summary.contains(QString::fromUtf8("—")));
}
