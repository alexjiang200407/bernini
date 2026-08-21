#include "VersionControl/GitVersionControl.h"

#include "VersionControl/git_cli.h"

#include <QFile>

namespace
{
	namespace fs = std::filesystem;

	/** A repository that removes itself, with one committed asset to change. */
	struct TempRepo
	{
		fs::path path;

		explicit TempRepo(std::string_view name) :
			path(fs::temp_directory_path() / ("bernini_changes_" + std::string(name)))
		{
			std::error_code ec;
			fs::remove_all(path, ec);
			fs::create_directories(path / "Data" / "Meshes");

			REQUIRE(editor::RunGit(path, { "init", "-q" }).Succeeded());
			REQUIRE(editor::RunGit(path, { "config", "user.email", "test@bernini" }).Succeeded());
			REQUIRE(editor::RunGit(path, { "config", "user.name", "Bernini Test" }).Succeeded());
		}

		~TempRepo()
		{
			std::error_code ec;
			fs::remove_all(path, ec);
		}

		TempRepo(const TempRepo&) = delete;
		TempRepo&
		operator=(const TempRepo&) = delete;

		void
		Write(std::string_view relative, const QByteArray& contents) const
		{
			const fs::path file = path / relative;
			fs::create_directories(file.parent_path());

			QFile out(QString::fromStdWString(file.generic_wstring()));
			REQUIRE(out.open(QIODevice::WriteOnly));
			REQUIRE(out.write(contents) == contents.size());
		}

		void
		Commit(std::string_view message) const
		{
			REQUIRE(editor::RunGit(path, { "add", "-A" }).Succeeded());
			REQUIRE(
				editor::RunGit(
					path,
					{ "commit", "-q", "-m", QString::fromUtf8(message.data(), message.size()) })
					.Succeeded());
		}
	};

	/** The one change in `changes`, so a failing case says which assertion rather than crashing. */
	const editor::PendingChange&
	Only(const std::vector<editor::PendingChange>& changes)
	{
		REQUIRE(changes.size() == 1);
		return changes.front();
	}
}

TEST_CASE("A project with nothing changed has no pending changes", "[vcs]")
{
	const TempRepo repo("clean");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");

	CHECK(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges().empty());
}

TEST_CASE("An asset the project has never seen is Added", "[vcs]")
{
	const TempRepo repo("untracked");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");
	repo.Write("Data/Meshes/squirrel.bmesh", "new");

	const auto change =
		Only(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges());

	CHECK(change.kind == editor::ChangeKind::kAdded);
	CHECK(change.path == QString("Data/Meshes/squirrel.bmesh"));
	CHECK_FALSE(change.renamedFrom.has_value());
}

// Staged or not is not a distinction the user has: Submit records and publishes in one action, so a
// new asset reads as Added either way.
TEST_CASE("An asset staged for the first time is still Added", "[vcs]")
{
	const TempRepo repo("staged_add");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");
	repo.Write("Data/Meshes/squirrel.bmesh", "new");
	REQUIRE(editor::RunGit(repo.path, { "add", "-A" }).Succeeded());

	CHECK(
		Only(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges()).kind ==
		editor::ChangeKind::kAdded);
}

// Without --untracked-files=all git collapses a new folder into one row naming the folder, and a
// list of assets to tick would then be a list of one.
TEST_CASE("A new folder of assets lists every asset in it", "[vcs]")
{
	const TempRepo repo("untracked_folder");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");

	repo.Write("Data/Levels/forest/trees.blevel", "new");
	repo.Write("Data/Levels/forest/rocks.blevel", "new");

	const auto changes = editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges();

	REQUIRE(changes.size() == 2);
	CHECK(changes[0].path == QString("Data/Levels/forest/rocks.blevel"));
	CHECK(changes[1].path == QString("Data/Levels/forest/trees.blevel"));
}

TEST_CASE("An asset written over is Modified", "[vcs]")
{
	const TempRepo repo("modified");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");
	repo.Write("Data/Meshes/coyote.bmesh", "two");

	const auto change =
		Only(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges());

	CHECK(change.kind == editor::ChangeKind::kModified);
	CHECK(change.path == QString("Data/Meshes/coyote.bmesh"));
}

TEST_CASE("An asset removed from disk is Deleted", "[vcs]")
{
	const TempRepo repo("deleted");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");
	fs::remove(repo.path / "Data/Meshes/coyote.bmesh");

	const auto change =
		Only(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges());

	CHECK(change.kind == editor::ChangeKind::kDeleted);
	CHECK(change.path == QString("Data/Meshes/coyote.bmesh"));
}

// Reachable by working partly through a terminal: staged as new, then deleted outside git. It was
// never submitted and is not on disk, so there is nothing for the user to tick.
TEST_CASE("An asset staged and then deleted is not a pending change at all", "[vcs]")
{
	const TempRepo repo("staged_then_deleted");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");

	repo.Write("Data/Meshes/squirrel.bmesh", "new");
	REQUIRE(editor::RunGit(repo.path, { "add", "-A" }).Succeeded());
	fs::remove(repo.path / "Data/Meshes/squirrel.bmesh");

	CHECK(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges().empty());
}

// The same shape, but the asset *was* submitted before, so removing it is a deletion to publish.
TEST_CASE("A submitted asset staged then deleted is Deleted", "[vcs]")
{
	const TempRepo repo("modified_then_deleted");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");

	repo.Write("Data/Meshes/coyote.bmesh", "two");
	REQUIRE(editor::RunGit(repo.path, { "add", "-A" }).Succeeded());
	fs::remove(repo.path / "Data/Meshes/coyote.bmesh");

	const auto change =
		Only(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges());

	CHECK(change.kind == editor::ChangeKind::kDeleted);
	CHECK(change.path == QString("Data/Meshes/coyote.bmesh"));
}

TEST_CASE("An asset moved somewhere else is Renamed, and says where from", "[vcs]")
{
	const TempRepo repo("renamed");
	repo.Write("Data/Meshes/coyote.bmesh", "content worth matching on");
	repo.Commit("first");
	REQUIRE(
		editor::RunGit(
			repo.path,
			{ "mv" },
			{ "Data/Meshes/coyote.bmesh", "Data/Meshes/wolf.bmesh" })
			.Succeeded());

	const auto change =
		Only(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges());

	CHECK(change.kind == editor::ChangeKind::kRenamed);
	CHECK(change.path == QString("Data/Meshes/wolf.bmesh"));
	REQUIRE(change.renamedFrom.has_value());
	CHECK(*change.renamedFrom == QString("Data/Meshes/coyote.bmesh"));
}

// -z exists for these two: without it git C-quotes the path and the name comes back mangled.
TEST_CASE("A path with a space in it survives", "[vcs]")
{
	const TempRepo repo("spaced_path");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");
	repo.Write("Data/Meshes/arctic wolf.bmesh", "new");

	CHECK(
		Only(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges()).path ==
		QString("Data/Meshes/arctic wolf.bmesh"));
}

TEST_CASE("A path outside ASCII survives", "[vcs]")
{
	const TempRepo repo("utf8_path");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");
	repo.Write("Data/Meshes/\xE3\x82\xAD\xE3\x83\x84\xE3\x83\x8D.bmesh", "new");

	CHECK(
		Only(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges()).path ==
		QString::fromUtf8("Data/Meshes/\xE3\x82\xAD\xE3\x83\x84\xE3\x83\x8D.bmesh"));
}

// A bake nobody submits is not a pending change, and the test project ignores two whole categories
// of them.
TEST_CASE("An asset the project ignores is not a pending change", "[vcs]")
{
	const TempRepo repo("ignored");
	repo.Write(".gitignore", "/Data/Textures/\n*.bvat\n");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");

	repo.Write("Data/Textures/coyote_albedo.ktx2", "baked");
	repo.Write("Data/Meshes/coyote.bvat", "baked");
	repo.Write("Data/Meshes/squirrel.bmesh", "new");

	const auto change =
		Only(editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges());

	CHECK(change.path == QString("Data/Meshes/squirrel.bmesh"));
}

TEST_CASE("Changes come back ordered by path", "[vcs]")
{
	const TempRepo repo("ordered");
	repo.Write("Data/Meshes/coyote.bmesh", "one");
	repo.Commit("first");

	repo.Write("Data/Meshes/coyote.bmesh", "two");
	repo.Write("Data/Meshes/apples.bmesh", "new");
	repo.Write("Data/Meshes/squirrel.bmesh", "new");

	const auto changes = editor::GitVersionControl(repo.path, repo.path / "Data").ListChanges();

	REQUIRE(changes.size() == 3);
	CHECK(changes[0].path == QString("Data/Meshes/apples.bmesh"));
	CHECK(changes[1].path == QString("Data/Meshes/coyote.bmesh"));
	CHECK(changes[2].path == QString("Data/Meshes/squirrel.bmesh"));
}

TEST_CASE("A project that is not in a repository cannot be read", "[vcs]")
{
	const fs::path  plain = fs::temp_directory_path() / "bernini_changes_no_repo";
	std::error_code ec;
	fs::remove_all(plain, ec);
	fs::create_directories(plain);

	CHECK_THROWS_AS(
		editor::GitVersionControl(plain, plain / "Data").ListChanges(),
		std::runtime_error);

	fs::remove_all(plain, ec);
}
