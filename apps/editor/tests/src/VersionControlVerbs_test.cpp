#include "util/SharedProject.h"

using namespace editor::test;

TEST_CASE("Submitting an asset publishes it to the shared project", "[vcs]")
{
	const SharedProject shared("submit");
	Write(shared.bob / "Data/Meshes/squirrel.bmesh", "new");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	const auto outcome = bob.Submit({ "Data/Meshes/squirrel.bmesh" }, "bob adds a squirrel");

	CHECK(outcome.status == editor::VersionControlStatus::kDone);
	CHECK(bob.ListChanges().empty());

	// The proof it was published rather than only recorded: the other clone can see it.
	editor::GitVersionControl alice(shared.alice, shared.alice / "Data");
	REQUIRE(alice.GetLatest().status == editor::VersionControlStatus::kDone);
	CHECK(fs::exists(shared.alice / "Data/Meshes/squirrel.bmesh"));
}

TEST_CASE("Submitting refuses when the shared project has moved on, and names the assets", "[vcs]")
{
	const SharedProject shared("submit_behind");
	shared.AlicePublishes("alice's version");

	Write(shared.bob / "Data/Meshes/squirrel.bmesh", "new");
	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	const auto outcome = bob.Submit({ "Data/Meshes/squirrel.bmesh" }, "bob adds a squirrel");

	CHECK(outcome.status == editor::VersionControlStatus::kWorkHasMovedOn);
	CHECK(outcome.assets == std::vector<QString>{ "Data/Meshes/coyote.bmesh" });

	// Refused before anything was recorded: the asset is still waiting to be submitted.
	REQUIRE(bob.ListChanges().size() == 1);
	CHECK(bob.ListChanges().front().path == QString("Data/Meshes/squirrel.bmesh"));
}

TEST_CASE("Submitting refuses when nobody has said who is submitting", "[vcs]")
{
	const SharedProject shared("submit_no_identity");
	// Set empty rather than unset: this machine has a global identity that would otherwise stand in.
	REQUIRE(editor::RunGit(shared.bob, { "config", "user.email", "" }).Succeeded());

	Write(shared.bob / "Data/Meshes/squirrel.bmesh", "new");
	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");

	const auto outcome = bob.Submit({ "Data/Meshes/squirrel.bmesh" }, "bob adds a squirrel");

	CHECK(outcome.status == editor::VersionControlStatus::kNoIdentity);
}

TEST_CASE("Submitting nothing is refused rather than recording an empty submission", "[vcs]")
{
	const SharedProject       shared("submit_nothing");
	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");

	const auto nothingChosen = bob.Submit({}, "nothing");
	CHECK(nothingChosen.status == editor::VersionControlStatus::kNothingToDo);

	const auto unchanged = bob.Submit({ "Data/Meshes/coyote.bmesh" }, "unchanged");
	CHECK(unchanged.status == editor::VersionControlStatus::kNothingToDo);
}

TEST_CASE("Get Latest fast-forwards when this project has submitted nothing", "[vcs]")
{
	const SharedProject shared("get_latest");
	shared.AlicePublishes("alice's version");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	const auto                outcome = bob.GetLatest();

	CHECK(outcome.status == editor::VersionControlStatus::kDone);
	CHECK(Read(shared.bob / "Data/Meshes/coyote.bmesh") == QByteArray("alice's version"));
	CHECK_FALSE(MergeInProgress(shared.bob));
}

TEST_CASE("Get Latest with nothing to get is refused rather than doing nothing quietly", "[vcs]")
{
	const SharedProject shared("get_latest_current");

	const auto outcome = editor::GitVersionControl(shared.bob, shared.bob / "Data").GetLatest();

	CHECK(outcome.status == editor::VersionControlStatus::kNothingToDo);
}

// ADR-8: never a merge, so there is no conflicted state to be stranded in.
TEST_CASE("Get Latest refuses when both sides have submitted, and changes nothing", "[vcs]")
{
	const SharedProject shared("get_latest_diverged");
	shared.AlicePublishes("alice's version");

	Write(shared.bob / "Data/Meshes/coyote.bmesh", "bob's version");
	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	REQUIRE(editor::RunGit(shared.bob, { "add", "-A" }).Succeeded());
	REQUIRE(editor::RunGit(shared.bob, { "commit", "-q", "-m", "bob's change" }).Succeeded());

	const auto outcome = bob.GetLatest();
	CHECK(outcome.status == editor::VersionControlStatus::kWouldNotFastForward);
	CHECK(outcome.assets == std::vector<QString>{ "Data/Meshes/coyote.bmesh" });
	CHECK(Read(shared.bob / "Data/Meshes/coyote.bmesh") == QByteArray("bob's version"));
	CHECK_FALSE(MergeInProgress(shared.bob));
}

// A fast-forward would still overwrite an unsubmitted edit, and git would stop halfway through it.
TEST_CASE(
	"Get Latest refuses when an unsubmitted asset is in the way, and changes nothing",
	"[vcs]")
{
	const SharedProject shared("get_latest_dirty");
	shared.AlicePublishes("alice's version");

	Write(shared.bob / "Data/Meshes/coyote.bmesh", "bob is still working on this");
	const auto outcome = editor::GitVersionControl(shared.bob, shared.bob / "Data").GetLatest();
	CHECK(outcome.status == editor::VersionControlStatus::kAssetsInTheWay);
	CHECK(outcome.assets == std::vector<QString>{ "Data/Meshes/coyote.bmesh" });
	CHECK(
		Read(shared.bob / "Data/Meshes/coyote.bmesh") ==
		QByteArray("bob is still working on this"));
	CHECK_FALSE(MergeInProgress(shared.bob));
}

// An unsubmitted edit to an asset the update does not touch is not in anybody's way.
TEST_CASE("Get Latest fast-forwards past an unsubmitted asset it does not touch", "[vcs]")
{
	const SharedProject shared("get_latest_dirty_elsewhere");
	shared.AlicePublishes("alice's version");

	Write(shared.bob / "Data/Meshes/squirrel.bmesh", "bob's new asset");
	const auto outcome = editor::GitVersionControl(shared.bob, shared.bob / "Data").GetLatest();

	CHECK(outcome.status == editor::VersionControlStatus::kDone);
	CHECK(Read(shared.bob / "Data/Meshes/coyote.bmesh") == QByteArray("alice's version"));
	CHECK(Read(shared.bob / "Data/Meshes/squirrel.bmesh") == QByteArray("bob's new asset"));
}

#ifndef _WIN32
// The race ADR-4 cannot design away: the shared project moves between the check and the publish. What
// it can insist on is that nothing is left recorded-but-unpublished, so the asset is still pending.
TEST_CASE("A submission that cannot be published leaves nothing recorded", "[vcs]")
{
	const SharedProject shared("submit_push_fails");
	Write(shared.bob / "Data/Meshes/squirrel.bmesh", "new");
	MakeReadOnly(shared.origin);

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	const auto outcome = bob.Submit({ "Data/Meshes/squirrel.bmesh" }, "bob adds a squirrel");
	CHECK(outcome.status == editor::VersionControlStatus::kWorkHasMovedOn);

	// Nothing published, and nothing recorded either: the asset is exactly where it was.
	REQUIRE(bob.ListChanges().size() == 1);
	CHECK(bob.ListChanges().front().path == QString("Data/Meshes/squirrel.bmesh"));

	const auto ahead = editor::RunGit(shared.bob, { "rev-list", "--count", "@{upstream}..HEAD" });
	REQUIRE(ahead.Succeeded());
	CHECK(QString::fromUtf8(ahead.out).trimmed() == QString("0"));
}
#endif

TEST_CASE("Reverting puts a submitted asset back as it was", "[vcs]")
{
	const SharedProject shared("revert_modified");
	Write(shared.bob / "Data/Meshes/coyote.bmesh", "ruined");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	const auto                outcome = bob.Revert({ "Data/Meshes/coyote.bmesh" });

	CHECK(outcome.status == editor::VersionControlStatus::kDone);
	CHECK(Read(shared.bob / "Data/Meshes/coyote.bmesh") == shared.seeded);
	CHECK(bob.ListChanges().empty());
}

TEST_CASE("Reverting a deleted asset brings it back", "[vcs]")
{
	const SharedProject shared("revert_deleted");
	fs::remove(shared.bob / "Data/Meshes/coyote.bmesh");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	REQUIRE(
		bob.Revert({ "Data/Meshes/coyote.bmesh" }).status == editor::VersionControlStatus::kDone);

	CHECK(Read(shared.bob / "Data/Meshes/coyote.bmesh") == shared.seeded);
	CHECK(bob.ListChanges().empty());
}

// There is no earlier version of an asset nobody ever submitted, so undoing it removes it.
TEST_CASE("Reverting an asset that was never submitted removes it", "[vcs]")
{
	const SharedProject shared("revert_added");
	WriteMesh(shared.bob / "Data/Meshes/squirrel.bmesh");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	REQUIRE(
		bob.Revert({ "Data/Meshes/squirrel.bmesh" }).status == editor::VersionControlStatus::kDone);

	CHECK_FALSE(fs::exists(shared.bob / "Data/Meshes/squirrel.bmesh"));
	CHECK(bob.ListChanges().empty());
}

TEST_CASE("Reverting one asset leaves the others alone", "[vcs]")
{
	const SharedProject shared("revert_one");
	Write(shared.bob / "Data/Meshes/coyote.bmesh", "ruined");
	Write(shared.bob / "Data/Meshes/squirrel.bmesh", "keep me");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	REQUIRE(
		bob.Revert({ "Data/Meshes/coyote.bmesh" }).status == editor::VersionControlStatus::kDone);

	CHECK(Read(shared.bob / "Data/Meshes/coyote.bmesh") == shared.seeded);
	CHECK(Read(shared.bob / "Data/Meshes/squirrel.bmesh") == QByteArray("keep me"));
}

// ADR-10, reached through the verb rather than tested in isolation. Alice drops a texture and the
// material of hers that used it -- self-consistent -- while Bob has a material of his own that also
// routes from it. Neither is wrong alone; the tree they would make together is.
TEST_CASE("Get Latest refuses a deletion another asset still needs, and changes nothing", "[vcs]")
{
	const SharedProject shared("get_latest_still_used");

	WriteTexture(shared.alice / "Data/textures_src/albedo.ktx2");
	WriteMaterial(shared.alice / "Data/Materials/alice.bmaterial", "textures_src/albedo.ktx2");
	editor::GitVersionControl alice(shared.alice, shared.alice / "Data");
	REQUIRE(
		alice
			.Submit(
				{ "Data/textures_src/albedo.ktx2", "Data/Materials/alice.bmaterial" },
				"alice adds a texture")
			.status == editor::VersionControlStatus::kDone);

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	REQUIRE(bob.GetLatest().status == editor::VersionControlStatus::kDone);

	WriteMaterial(shared.bob / "Data/Materials/bob.bmaterial", "textures_src/albedo.ktx2");

	fs::remove(shared.alice / "Data/textures_src/albedo.ktx2");
	fs::remove(shared.alice / "Data/Materials/alice.bmaterial");
	REQUIRE(
		alice
			.Submit(
				{ "Data/textures_src/albedo.ktx2", "Data/Materials/alice.bmaterial" },
				"alice drops the texture")
			.status == editor::VersionControlStatus::kDone);

	const auto outcome = bob.GetLatest();

	CHECK(outcome.status == editor::VersionControlStatus::kAssetsStillInUse);
	CHECK(
		outcome.assets ==
		std::vector<QString>{ "Data/textures_src/albedo.ktx2", "Data/Materials/bob.bmaterial" });

	// Nothing moved: the texture is still there and so is the material that needs it.
	CHECK(fs::exists(shared.bob / "Data/textures_src/albedo.ktx2"));
	CHECK(fs::exists(shared.bob / "Data/Materials/alice.bmaterial"));
	CHECK_FALSE(MergeInProgress(shared.bob));
}

// The same rule on the other verb that deletes: reverting an asset that was never submitted removes
// it, and something of the user's own may still need it.
TEST_CASE("Reverting refuses to remove an asset another still needs", "[vcs]")
{
	const SharedProject shared("revert_still_used");
	WriteTexture(shared.bob / "Data/textures_src/albedo.ktx2");
	WriteMaterial(shared.bob / "Data/Materials/bob.bmaterial", "textures_src/albedo.ktx2");

	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");
	const auto                outcome = bob.Revert({ "Data/textures_src/albedo.ktx2" });

	CHECK(outcome.status == editor::VersionControlStatus::kAssetsStillInUse);
	CHECK(fs::exists(shared.bob / "Data/textures_src/albedo.ktx2"));
}

TEST_CASE("An asset outside the project is refused before it reaches the backend", "[vcs]")
{
	const SharedProject       shared("fence");
	editor::GitVersionControl bob(shared.bob, shared.bob / "Data");

	CHECK_THROWS_AS(
		bob.Submit({ "../alice/Data/Meshes/coyote.bmesh" }, "sneaky"),
		std::runtime_error);
	CHECK_THROWS_AS(bob.Revert({ "../alice/Data/Meshes/coyote.bmesh" }), std::runtime_error);
}
