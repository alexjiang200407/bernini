#include "VersionControl/GitVersionControl.h"

#include "VersionControl/git_cli.h"

#include <QFile>

namespace
{
	namespace fs = std::filesystem;

	QString
	ToQString(const fs::path& path)
	{
		return QString::fromStdWString(path.generic_wstring());
	}

	QByteArray
	Read(const fs::path& file)
	{
		QFile in(ToQString(file));
		REQUIRE(in.open(QIODevice::ReadOnly));
		return in.readAll();
	}

	void
	Write(const fs::path& file, const QByteArray& contents)
	{
		fs::create_directories(file.parent_path());

		QFile out(ToQString(file));
		REQUIRE(out.open(QIODevice::WriteOnly));
		REQUIRE(out.write(contents) == contents.size());
	}

	/**
	 * A shared project and two people working on it, which is the only shape most of these rules can
	 * be seen in at all: one clone publishes, the other finds out.
	 */
	struct SharedProject
	{
		fs::path root;
		fs::path origin;
		fs::path alice;
		fs::path bob;

		explicit SharedProject(std::string_view name) :
			root(fs::temp_directory_path() / ("bernini_verbs_" + std::string(name))),
			origin(root / "origin"), alice(root / "alice"), bob(root / "bob")
		{
			std::error_code ec;
			fs::remove_all(root, ec);
			fs::create_directories(root);

			REQUIRE(
				editor::RunGit(root, { "init", "--bare", "-q", "-b", "main", ToQString(origin) })
					.Succeeded());

			CloneInto(alice);
			Write(alice / "Data/Meshes/coyote.bmesh", "one");
			REQUIRE(editor::RunGit(alice, { "add", "-A" }).Succeeded());
			REQUIRE(editor::RunGit(alice, { "commit", "-q", "-m", "first" }).Succeeded());
			REQUIRE(editor::RunGit(alice, { "push", "-q", "-u", "origin", "main" }).Succeeded());

			CloneInto(bob);
		}

		~SharedProject()
		{
			// A case that made the shared project unwritable left a tree that cannot be removed.
			std::error_code ec;
			for (const auto& entry : fs::recursive_directory_iterator(root, ec))
			{
				fs::permissions(entry.path(), fs::perms::owner_write, fs::perm_options::add, ec);
			}
			fs::remove_all(root, ec);
		}

		SharedProject(const SharedProject&) = delete;
		SharedProject&
		operator=(const SharedProject&) = delete;

		void
		CloneInto(const fs::path& into) const
		{
			REQUIRE(
				editor::RunGit(root, { "clone", "-q", ToQString(origin), ToQString(into) })
					.Succeeded());
			REQUIRE(editor::RunGit(into, { "config", "user.email", "test@bernini" }).Succeeded());
			REQUIRE(editor::RunGit(into, { "config", "user.name", "Bernini Test" }).Succeeded());
		}

		/** Alice changes one asset and publishes it, so Bob's clone is behind. */
		void
		AlicePublishes(const QByteArray& contents) const
		{
			Write(alice / "Data/Meshes/coyote.bmesh", contents);
			editor::GitVersionControl vcs(alice);
			const auto outcome = vcs.Submit({ "Data/Meshes/coyote.bmesh" }, "alice's change");
			REQUIRE(outcome.status == editor::VersionControlStatus::kDone);
		}
	};

#ifndef _WIN32
	/** Makes every file and directory under `tree` unwritable, so a push into it fails. */
	void
	MakeReadOnly(const fs::path& tree)
	{
		for (const auto& entry : fs::recursive_directory_iterator(tree))
		{
			fs::permissions(entry.path(), fs::perms::owner_write, fs::perm_options::remove);
		}
		fs::permissions(tree, fs::perms::owner_write, fs::perm_options::remove);
	}
#endif

	bool
	MergeInProgress(const fs::path& clone)
	{
		return fs::exists(clone / ".git" / "MERGE_HEAD");
	}
}

TEST_CASE("Submitting an asset publishes it to the shared project", "[vcs]")
{
	const SharedProject shared("submit");
	Write(shared.bob / "Data/Meshes/squirrel.bmesh", "new");

	editor::GitVersionControl bob(shared.bob);
	const auto outcome = bob.Submit({ "Data/Meshes/squirrel.bmesh" }, "bob adds a squirrel");

	CHECK(outcome.status == editor::VersionControlStatus::kDone);
	CHECK(bob.ListChanges().empty());

	// The proof it was published rather than only recorded: the other clone can see it.
	editor::GitVersionControl alice(shared.alice);
	REQUIRE(alice.GetLatest().status == editor::VersionControlStatus::kDone);
	CHECK(fs::exists(shared.alice / "Data/Meshes/squirrel.bmesh"));
}

TEST_CASE("Submitting refuses when the shared project has moved on, and names the assets", "[vcs]")
{
	const SharedProject shared("submit_behind");
	shared.AlicePublishes("alice's version");

	Write(shared.bob / "Data/Meshes/squirrel.bmesh", "new");
	editor::GitVersionControl bob(shared.bob);
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
	editor::GitVersionControl bob(shared.bob);

	const auto outcome = bob.Submit({ "Data/Meshes/squirrel.bmesh" }, "bob adds a squirrel");

	CHECK(outcome.status == editor::VersionControlStatus::kNoIdentity);
}

TEST_CASE("Submitting nothing is refused rather than recording an empty submission", "[vcs]")
{
	const SharedProject       shared("submit_nothing");
	editor::GitVersionControl bob(shared.bob);

	const auto nothingChosen = bob.Submit({}, "nothing");
	CHECK(nothingChosen.status == editor::VersionControlStatus::kNothingToDo);

	const auto unchanged = bob.Submit({ "Data/Meshes/coyote.bmesh" }, "unchanged");
	CHECK(unchanged.status == editor::VersionControlStatus::kNothingToDo);
}

TEST_CASE("Get Latest fast-forwards when this project has submitted nothing", "[vcs]")
{
	const SharedProject shared("get_latest");
	shared.AlicePublishes("alice's version");

	editor::GitVersionControl bob(shared.bob);
	const auto                outcome = bob.GetLatest();

	CHECK(outcome.status == editor::VersionControlStatus::kDone);
	CHECK(Read(shared.bob / "Data/Meshes/coyote.bmesh") == QByteArray("alice's version"));
	CHECK_FALSE(MergeInProgress(shared.bob));
}

TEST_CASE("Get Latest with nothing to get is refused rather than doing nothing quietly", "[vcs]")
{
	const SharedProject shared("get_latest_current");

	const auto outcome = editor::GitVersionControl(shared.bob).GetLatest();

	CHECK(outcome.status == editor::VersionControlStatus::kNothingToDo);
}

// ADR-8: never a merge, so there is no conflicted state to be stranded in.
TEST_CASE("Get Latest refuses when both sides have submitted, and changes nothing", "[vcs]")
{
	const SharedProject shared("get_latest_diverged");
	shared.AlicePublishes("alice's version");

	Write(shared.bob / "Data/Meshes/coyote.bmesh", "bob's version");
	editor::GitVersionControl bob(shared.bob);
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
	const auto outcome = editor::GitVersionControl(shared.bob).GetLatest();
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
	const auto outcome = editor::GitVersionControl(shared.bob).GetLatest();

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

	editor::GitVersionControl bob(shared.bob);
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

	editor::GitVersionControl bob(shared.bob);
	const auto                outcome = bob.Revert({ "Data/Meshes/coyote.bmesh" });

	CHECK(outcome.status == editor::VersionControlStatus::kDone);
	CHECK(Read(shared.bob / "Data/Meshes/coyote.bmesh") == QByteArray("one"));
	CHECK(bob.ListChanges().empty());
}

TEST_CASE("Reverting a deleted asset brings it back", "[vcs]")
{
	const SharedProject shared("revert_deleted");
	fs::remove(shared.bob / "Data/Meshes/coyote.bmesh");

	editor::GitVersionControl bob(shared.bob);
	REQUIRE(
		bob.Revert({ "Data/Meshes/coyote.bmesh" }).status == editor::VersionControlStatus::kDone);

	CHECK(Read(shared.bob / "Data/Meshes/coyote.bmesh") == QByteArray("one"));
	CHECK(bob.ListChanges().empty());
}

// There is no earlier version of an asset nobody ever submitted, so undoing it removes it.
TEST_CASE("Reverting an asset that was never submitted removes it", "[vcs]")
{
	const SharedProject shared("revert_added");
	Write(shared.bob / "Data/Meshes/squirrel.bmesh", "new");

	editor::GitVersionControl bob(shared.bob);
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

	editor::GitVersionControl bob(shared.bob);
	REQUIRE(
		bob.Revert({ "Data/Meshes/coyote.bmesh" }).status == editor::VersionControlStatus::kDone);

	CHECK(Read(shared.bob / "Data/Meshes/coyote.bmesh") == QByteArray("one"));
	CHECK(Read(shared.bob / "Data/Meshes/squirrel.bmesh") == QByteArray("keep me"));
}

TEST_CASE("An asset outside the project is refused before it reaches the backend", "[vcs]")
{
	const SharedProject       shared("fence");
	editor::GitVersionControl bob(shared.bob);

	CHECK_THROWS_AS(
		bob.Submit({ "../alice/Data/Meshes/coyote.bmesh" }, "sneaky"),
		std::runtime_error);
	CHECK_THROWS_AS(bob.Revert({ "../alice/Data/Meshes/coyote.bmesh" }), std::runtime_error);
}
