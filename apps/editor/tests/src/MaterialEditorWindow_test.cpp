#include "Windows/MaterialEditor/material_io.h"

#include "util/QtSupport.h"

#include <QDir>
#include <QTemporaryDir>

#include <assetlib_structs/BMaterial.h>
#include <catch2/catch_test_macros.hpp>
#include <qbuffer.h>
#include <qcontainerfwd.h>

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
	CHECK_FALSE(editor::IsSameMaterialFile(QString(), "C:/Data/Materials/Leaf.bmaterial"));
}

TEST_CASE("An unsaved graph is never already default", "[materialeditor]")
{
	// Nothing on disk to bind: Save first. Enabling the button here would bind a path to nothing.
	CHECK_FALSE(editor::IsSameMaterialFile("C:/Data/Materials/Leaf.bmaterial", QString()));
}

TEST_CASE("The material the mesh already names is already default", "[materialeditor]")
{
	CHECK(
		editor::IsSameMaterialFile(
			"C:/Data/Materials/Leaf.bmaterial",
			"C:/Data/Materials/Leaf.bmaterial"));
}

TEST_CASE("A different material is not already default", "[materialeditor]")
{
	CHECK_FALSE(
		editor::IsSameMaterialFile(
			"C:/Data/Materials/Leaf.bmaterial",
			"C:/Data/Materials/Wood.bmaterial"));
}

TEST_CASE("The same file spelled differently is still already default", "[materialeditor]")
{
#if defined(_WIN32)
	// The .bmesh's path is resolved with std::filesystem (native separators); a file dialog hands back
	// forward slashes. Same file. Windows only: elsewhere the native separator already is a forward
	// slash, and a backslash is an ordinary character in a name rather than a separator at all.
	CHECK(
		editor::IsSameMaterialFile(
			"C:\\Data\\Materials\\Leaf.bmaterial",
			"C:/Data/Materials/Leaf.bmaterial"));
#endif

	// A data root that is not already normalised resolves through a parent segment.
	CHECK(
		editor::IsSameMaterialFile(
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

	CHECK(editor::IsSameMaterialFile(path, viaParent));
	CHECK_FALSE(editor::IsSameMaterialFile(path, QDir(dir.path()).filePath("Other.bmaterial")));
}

TEST_CASE("Two materials that do not exist are still told apart", "[materialeditor]")
{
	// A material can be deleted out from under a mesh that still names it. If the two compared equal
	// merely by both being absent, Set Default Material would grey out on every mesh.
	CHECK_FALSE(
		editor::IsSameMaterialFile("C:/Nowhere/Leaf.bmaterial", "C:/Nowhere/Wood.bmaterial"));
}

TEST_CASE("Case is not what tells two materials apart", "[materialeditor]")
{
	// Windows: the .bmesh's path comes back from std::filesystem, a file dialog's from the shell, and
	// they need not agree on case.
	CHECK(
		editor::IsSameMaterialFile(
			"C:/Data/Materials/Leaf.bmaterial",
			"C:/data/materials/leaf.bmaterial"));
}

TEST_CASE("A baked material lists the textures it names", "[materialeditor]")
{
	// "Show the current baked textures if any": the paths the material's last bake wrote, one per line,
	// so the artist can see what the mesh actually samples without opening the files.
	auto material                 = assetlib::BMaterial();
	material.shadingModel         = assetlib::ShadingModel::kPbr;
	material.pbr.baseColorTexture = "Textures/basecolor_a1b2.ktx2";
	material.pbr.normalTexture    = "Textures/normal_c3d4.ktx2";
	material.pbr.ormTexture       = "Textures/orm_e5f6.ktx2";

	const QString summary = editor::BakedTexturesSummary(material);

	CHECK(summary.contains("Textures/basecolor_a1b2.ktx2"));
	CHECK(summary.contains("Textures/normal_c3d4.ktx2"));
	CHECK(summary.contains("Textures/orm_e5f6.ktx2"));
}

TEST_CASE("A material with no baked triplet lists nothing", "[materialeditor]")
{
	// A material authored but never baked carries only routes, no triplet -- there is nothing baked to
	// show, and the empty string is what keeps the label hidden.
	auto material         = assetlib::BMaterial();
	material.shadingModel = assetlib::ShadingModel::kPbr;
	material.pbr.routes[0].texture =
		"Derived/SourceTextures/albedo.ktx2";  // a source route, not a baked map

	CHECK(editor::BakedTexturesSummary(material).isEmpty());
}

TEST_CASE(
	"A material baked without every map shows a dash for the one it lacks",
	"[materialeditor]")
{
	// Base colour and ORM baked, no normal routed: the missing map reads as a dash rather than a blank
	// that looks like a bug, and the listing still shows because something is baked.
	auto material                 = assetlib::BMaterial();
	material.shadingModel         = assetlib::ShadingModel::kPbr;
	material.pbr.baseColorTexture = "Textures/basecolor_a1b2.ktx2";
	material.pbr.ormTexture       = "Textures/orm_e5f6.ktx2";

	const QString summary = editor::BakedTexturesSummary(material);

	REQUIRE_FALSE(summary.isEmpty());
	CHECK(summary.contains(QString::fromUtf8("—")));
}

// Save All and Bake All act on the mesh rather than on the selected submesh, and the two rules that
// decides are here rather than in the window: which files a batch touches, and what it says
// afterwards. The window itself cannot be driven -- both end in a modal, and without a graphics
// device there are no submesh graphs to batch over in the first place.

TEST_CASE("A material two submeshes wear is one file to bake", "[materialeditor]")
{
	// Submeshes sharing a material share a graph, so the set hands the same path over once per
	// *graph* -- but a Save As can put a second graph on a path another already holds. Baking it
	// twice would decode, resize and re-encode every map a second time to write what is already
	// there.
	const QStringList paths = {
		"C:/Data/Materials/Leaf.bmaterial",
		"C:/Data/Materials/Bark.bmaterial",
		"C:/Data/Materials/Leaf.bmaterial",
	};

	CHECK(
		editor::UniqueMaterialFiles(paths) ==
		QStringList{ "C:/Data/Materials/Leaf.bmaterial", "C:/Data/Materials/Bark.bmaterial" });
}

TEST_CASE("The same file spelled two ways is one file to bake", "[materialeditor]")
{
	// The mesh's own reference is resolved against the data root; a file dialog hands one back
	// verbatim. Compared as strings these are two files, and the bake would run twice.
	const QStringList paths = {
		"C:/Data/Materials/Leaf.bmaterial",
		"C:/Data/Textures/../Materials/Leaf.bmaterial",
	};

	CHECK(editor::UniqueMaterialFiles(paths).size() == 1);
}

TEST_CASE("A graph with no file is nothing to bake", "[materialeditor]")
{
	// The default sphere, and any submesh the mesh never bound. Save As is what gives one a file;
	// until then there is no path to hand a bake.
	CHECK(
		editor::UniqueMaterialFiles({ QString(), "C:/Data/Materials/Leaf.bmaterial", QString() })
			.size() == 1);
}

TEST_CASE("Nothing is said when every material was written", "[materialeditor]")
{
	// The common case, and the one a dialog would only get in the way of: the panel's own refresh --
	// the path label, the stale marker -- is the report.
	auto clean  = editor::MaterialSaveResult();
	clean.saved = 3;

	CHECK(editor::MaterialSaveSummary(clean).isEmpty());
}

TEST_CASE("A skipped submesh says how to give it a file", "[materialeditor]")
{
	// Silently writing four of five materials is the failure mode this exists to prevent: the user
	// has to be told the fifth was left, and that Save As is what fixes it.
	auto skipped    = editor::MaterialSaveResult();
	skipped.saved   = 4;
	skipped.unsaved = 1;

	const QString summary = editor::MaterialSaveSummary(skipped);

	REQUIRE_FALSE(summary.isEmpty());
	CHECK(summary.contains("4 materials"));
	CHECK(summary.contains("1 submesh"));
	CHECK_FALSE(summary.contains("1 submeshes"));
	CHECK(summary.contains("Save As"));
}

TEST_CASE("A material that could not be written is named", "[materialeditor]")
{
	// A read-only file or a data root that has gone. The others are still written -- one bad path
	// must not cost the rest their save -- so the summary has to say which one it was.
	auto failed   = editor::MaterialSaveResult();
	failed.saved  = 1;
	failed.failed = { "C:/Data/Materials/Leaf.bmaterial" };

	const QString summary = editor::MaterialSaveSummary(failed);

	CHECK(summary.contains("1 material"));
	CHECK(summary.contains("Leaf.bmaterial"));
}

TEST_CASE("Nothing written is not reported as saving nothing", "[materialeditor]")
{
	// Every graph skipped -- the default sphere, or a mesh nothing has been saved for yet. "Saved 0
	// materials." leads with a non-event; what the user needs is the reason and the way out.
	auto none    = editor::MaterialSaveResult();
	none.unsaved = 2;

	const QString summary = editor::MaterialSaveSummary(none);

	CHECK_FALSE(summary.contains("Saved 0"));
	CHECK(summary.startsWith("Skipped 2 submeshes"));
}

TEST_CASE("A material the mesh could not be made to name is reported once", "[materialeditor]")
{
	// The `.bmaterial` landed and the `.bmesh` did not, which is neither a save failure nor a skip:
	// the material is on disk, and the mesh still points somewhere else. Reported in the same summary
	// rather than in a modal per submesh -- whatever stopped the write is the mesh file, which every
	// submesh of the graph shares.
	auto partial       = editor::MaterialSaveResult();
	partial.saved      = 2;
	partial.unattached = { "C:/Data/Materials/Leaf.bmaterial" };

	const QString summary = editor::MaterialSaveSummary(partial);

	CHECK(summary.contains("Saved 2 materials"));
	CHECK(summary.contains("Leaf.bmaterial"));
	CHECK_FALSE(summary.contains("Could not write"));
}

TEST_CASE("The Material Editor holds its preview mesh open", "[materialeditor]")
{
	// Delete Cascade asks each panel what it is holding and refuses to remove any of it. The
	// Material Editor used to answer with its materials alone, so a `.bmesh` dropped onto its
	// preview was invisible to that check and went, while the panel carried on drawing it and
	// binding materials into it.
	const QStringList materials{ "C:/Data/Materials/Leaf.bmaterial" };

	const QStringList held =
		editor::HeldOpenByMaterialEditor(materials, "C:/Data/Meshes/tree.bmesh");
	REQUIRE(held.size() == 2);
	CHECK(held[0] == "C:/Data/Materials/Leaf.bmaterial");
	CHECK(held[1] == "C:/Data/Meshes/tree.bmesh");
}

TEST_CASE("The default sphere holds nothing open", "[materialeditor]")
{
	// A preview showing the built-in sphere has no file behind it, and an empty path in the
	// held-open list would match a deletion of the data root itself.
	const QStringList held =
		editor::HeldOpenByMaterialEditor({ "C:/Data/Materials/Leaf.bmaterial" }, {});
	CHECK(held == QStringList{ "C:/Data/Materials/Leaf.bmaterial" });
}
