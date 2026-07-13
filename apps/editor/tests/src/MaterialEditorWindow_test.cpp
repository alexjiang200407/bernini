#include "Windows/MaterialEditor/MaterialEditorWindow.h"

#include "util/QtSupport.h"

#include <QDir>
#include <QTemporaryDir>

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
