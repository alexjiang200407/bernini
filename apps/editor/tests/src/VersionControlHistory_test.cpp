#include "util/SharedProject.h"

using namespace editor::test;

namespace
{
	/** Bob submits one asset, so the history has something in it with a known message. */
	void
	BobSubmits(const SharedProject& shared, const char* asset, const char* message)
	{
		WriteMesh(shared.bob / asset);

		editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
		REQUIRE(
			bob.Submit({ QString::fromUtf8(asset) }, QString::fromUtf8(message)).status ==
			editor::VersionControlStatus::kDone);
	}
}

TEST_CASE("The history lists submissions newest first, each with what it touched", "[vcs]")
{
	const SharedProject shared("history");
	BobSubmits(shared, "Data/Meshes/squirrel.bmesh", "bob adds a squirrel");
	BobSubmits(shared, "Data/Meshes/apples.bmesh", "bob adds some apples");

	const auto history = editor::GitVersionControl(shared.bob, shared.bob / "Data").ListHistory(10);

	REQUIRE(history.size() == 3);
	CHECK(history[0].message == QString("bob adds some apples"));
	CHECK(history[0].assets == std::vector<QString>{ "Data/Meshes/apples.bmesh" });
	CHECK(history[1].message == QString("bob adds a squirrel"));
	CHECK(history[1].assets == std::vector<QString>{ "Data/Meshes/squirrel.bmesh" });
	CHECK(history[2].message == QString("first"));

	CHECK(history[0].author == QString("Bernini Test"));
	CHECK(history[0].when.isValid());
	CHECK_FALSE(history[0].id.isEmpty());
}

TEST_CASE("The history stops at the number of submissions asked for", "[vcs]")
{
	const SharedProject shared("history_limit");
	BobSubmits(shared, "Data/Meshes/squirrel.bmesh", "bob adds a squirrel");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");

	CHECK(bob.ListHistory(1).size() == 1);
	CHECK_THROWS_AS(bob.ListHistory(0), std::runtime_error);
}

// ADR-9: the record of what happened is most of what a history is for, so undoing adds to it.
TEST_CASE("Undoing a submission takes the asset away and leaves the entry behind", "[vcs]")
{
	const SharedProject shared("undo");
	BobSubmits(shared, "Data/Meshes/squirrel.bmesh", "bob adds a squirrel");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	const auto                undone = bob.ListHistory(1).front();

	CHECK(bob.UndoSubmission(undone.id).status == editor::VersionControlStatus::kDone);

	CHECK_FALSE(fs::exists(shared.bob / "Data/Meshes/squirrel.bmesh"));

	const auto history = bob.ListHistory(10);
	REQUIRE(history.size() == 3);
	CHECK(history[0].assets == std::vector<QString>{ "Data/Meshes/squirrel.bmesh" });
	CHECK(history[1].id == undone.id);
}

TEST_CASE("An undo is published like any other submission", "[vcs]")
{
	const SharedProject shared("undo_published");
	BobSubmits(shared, "Data/Meshes/squirrel.bmesh", "bob adds a squirrel");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	REQUIRE(
		bob.UndoSubmission(bob.ListHistory(1).front().id).status ==
		editor::VersionControlStatus::kDone);

	editor::GitVersionControl alice(shared.alice, shared.alice / "Data");
	REQUIRE(alice.GetLatest().status == editor::VersionControlStatus::kDone);
	CHECK_FALSE(fs::exists(shared.alice / "Data/Meshes/squirrel.bmesh"));
}

TEST_CASE("Undoing refuses when a later submission changed the same asset", "[vcs]")
{
	const SharedProject shared("undo_changed_since");

	// A texture rather than a mesh: the guard reads every mesh in the project, and this case needs an
	// asset whose *contents* can differ between two submissions without being a real container.
	WriteTexture(shared.bob / "Data/textures_src/albedo.ktx2");
	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	REQUIRE(
		bob.Submit({ "Data/textures_src/albedo.ktx2" }, "bob adds a texture").status ==
		editor::VersionControlStatus::kDone);
	const auto undone = bob.ListHistory(1).front();

	Write(shared.bob / "Data/textures_src/albedo.ktx2", "bob keeps working on it");
	REQUIRE(
		bob.Submit({ "Data/textures_src/albedo.ktx2" }, "bob edits the texture").status ==
		editor::VersionControlStatus::kDone);

	const auto outcome = bob.UndoSubmission(undone.id);

	CHECK(outcome.status == editor::VersionControlStatus::kAssetsChangedSince);

	// Refused whole: the asset is untouched and no half-finished undo is left in the way.
	CHECK(
		Read(shared.bob / "Data/textures_src/albedo.ktx2") ==
		QByteArray("bob keeps working on it"));
	CHECK(bob.ListChanges().empty());
	CHECK(bob.ListHistory(10).size() == 3);
}

TEST_CASE("Undoing refuses while there is unsubmitted work in the way", "[vcs]")
{
	const SharedProject shared("undo_dirty");
	BobSubmits(shared, "Data/Meshes/squirrel.bmesh", "bob adds a squirrel");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	const auto                undone = bob.ListHistory(1).front();

	Write(shared.bob / "Data/Meshes/apples.bmesh", "unsubmitted");
	const auto outcome = bob.UndoSubmission(undone.id);

	CHECK(outcome.status == editor::VersionControlStatus::kAssetsInTheWay);
	CHECK(outcome.assets == std::vector<QString>{ "Data/Meshes/apples.bmesh" });
	CHECK(fs::exists(shared.bob / "Data/Meshes/squirrel.bmesh"));
}

TEST_CASE("Undoing refuses when the shared project has moved on", "[vcs]")
{
	const SharedProject shared("undo_behind");
	BobSubmits(shared, "Data/Meshes/squirrel.bmesh", "bob adds a squirrel");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	const auto                undone = bob.ListHistory(1).front();

	editor::GitVersionControl alice(shared.alice, shared.alice / "Data");
	REQUIRE(alice.GetLatest().status == editor::VersionControlStatus::kDone);
	WriteMesh(shared.alice / "Data/Meshes/apples.bmesh");
	REQUIRE(
		alice.Submit({ "Data/Meshes/apples.bmesh" }, "alice adds apples").status ==
		editor::VersionControlStatus::kDone);

	CHECK(bob.UndoSubmission(undone.id).status == editor::VersionControlStatus::kWorkHasMovedOn);
	CHECK(fs::exists(shared.bob / "Data/Meshes/squirrel.bmesh"));
}

// ADR-10's third verb: undoing a submission that added an asset takes it away again.
TEST_CASE("Undoing refuses to remove an asset another still needs", "[vcs]")
{
	const SharedProject shared("undo_still_used");

	WriteTexture(shared.bob / "Data/textures_src/albedo.ktx2");
	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	REQUIRE(
		bob.Submit({ "Data/textures_src/albedo.ktx2" }, "bob adds a texture").status ==
		editor::VersionControlStatus::kDone);
	const auto undone = bob.ListHistory(1).front();

	WriteMaterial(shared.bob / "Data/Materials/bob.bmaterial", "textures_src/albedo.ktx2");
	REQUIRE(
		bob.Submit({ "Data/Materials/bob.bmaterial" }, "bob adds a material").status ==
		editor::VersionControlStatus::kDone);

	const auto outcome = bob.UndoSubmission(undone.id);

	CHECK(outcome.status == editor::VersionControlStatus::kAssetsStillInUse);
	CHECK(fs::exists(shared.bob / "Data/textures_src/albedo.ktx2"));
}
