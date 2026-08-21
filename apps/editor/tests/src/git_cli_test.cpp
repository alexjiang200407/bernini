#include "VersionControl/git_cli.h"

#include <QFile>

namespace
{
	namespace fs = std::filesystem;

	/** A directory that removes itself, so a case that fails by throwing still cleans up. */
	struct TempTree
	{
		fs::path path;

		explicit TempTree(std::string_view name) :
			path(fs::temp_directory_path() / ("bernini_vcs_" + std::string(name)))
		{
			std::error_code ec;
			fs::remove_all(path, ec);
			fs::create_directories(path);
		}

		~TempTree()
		{
			std::error_code ec;
			fs::remove_all(path, ec);
		}

		TempTree(const TempTree&) = delete;
		TempTree&
		operator=(const TempTree&) = delete;
	};

	/** A TempTree with a repository in it, identified so a commit would be possible. */
	struct TempRepo : TempTree
	{
		explicit TempRepo(std::string_view name) : TempTree(name)
		{
			REQUIRE(editor::RunGit(path, { "init", "-q" }).Succeeded());
			REQUIRE(editor::RunGit(path, { "config", "user.email", "test@bernini" }).Succeeded());
			REQUIRE(editor::RunGit(path, { "config", "user.name", "Bernini Test" }).Succeeded());
		}
	};

	void
	WriteFile(const fs::path& path, const QByteArray& contents)
	{
		fs::create_directories(path.parent_path());

		QFile file(QString::fromStdWString(path.generic_wstring()));
		REQUIRE(file.open(QIODevice::WriteOnly));
		REQUIRE(file.write(contents) == contents.size());
	}

	QByteArray
	Trimmed(const editor::GitCommandResult& result)
	{
		return QByteArray(result.out).trimmed();
	}
}

TEST_CASE("The repository root is found from a directory deep inside it", "[vcs]")
{
	const TempRepo repo("root_from_nested");
	fs::create_directories(repo.path / "Data" / "Meshes" / "animals");

	const auto found = editor::FindRepositoryRoot(repo.path / "Data" / "Meshes" / "animals");

	REQUIRE(found.has_value());
	// git resolves symlinks in what it reports, and a temp directory is one on macOS.
	CHECK(fs::canonical(*found) == fs::canonical(repo.path));
}

TEST_CASE("A project file names the repository its directory is in", "[vcs]")
{
	const TempRepo repo("root_from_file");
	WriteFile(repo.path / "Test Project.berniniproject", "{}");

	const auto found = editor::FindRepositoryRoot(repo.path / "Test Project.berniniproject");

	REQUIRE(found.has_value());
	CHECK(fs::canonical(*found) == fs::canonical(repo.path));
}

TEST_CASE("A directory outside any repository has no root", "[vcs]")
{
	const TempTree plain("no_repo");

	CHECK_FALSE(editor::FindRepositoryRoot(plain.path).has_value());
}

TEST_CASE("A directory that does not exist fails instead of starting git", "[vcs]")
{
	const auto result = editor::RunGit(
		fs::temp_directory_path() / "bernini_vcs_absent" / "nowhere",
		{ "rev-parse", "--show-toplevel" });

	CHECK_FALSE(result.Succeeded());
	CHECK_FALSE(result.exitCode.has_value());
}

TEST_CASE("A failing command surfaces what git said on stderr", "[vcs]")
{
	const TempRepo repo("stderr");

	const auto result = editor::RunGit(repo.path, { "cat-file", "blob", "not-a-hash" });

	CHECK_FALSE(result.Succeeded());
	CHECK_FALSE(result.err.isEmpty());
}

TEST_CASE("An argument containing a space arrives as one argument", "[vcs]")
{
	const TempRepo repo("spaced_argument");

	REQUIRE(editor::RunGit(repo.path, { "config", "bernini.test", "two words" }).Succeeded());
	const auto read = editor::RunGit(repo.path, { "config", "--get", "bernini.test" });

	REQUIRE(read.Succeeded());
	CHECK(Trimmed(read) == QByteArray("two words"));
}

// The whole point of the `paths` parameter: git reads a leading dash as an option, and an artist is
// free to name an asset `-o`.
TEST_CASE("An asset whose name looks like an option is still a path", "[vcs]")
{
	const TempRepo repo("dash_path");
	WriteFile(repo.path / "-o", "content");

	const auto added = editor::RunGit(repo.path, { "add" }, { "-o" });
	CHECK(added.Succeeded());

	const auto listed = editor::RunGit(repo.path, { "ls-files" });
	REQUIRE(listed.Succeeded());
	CHECK(listed.out.contains("-o"));

	// The same name passed as an argument is what the separator exists to prevent.
	CHECK_FALSE(editor::RunGit(repo.path, { "add", "-o" }).Succeeded());
}

TEST_CASE("A path is refused when it does not resolve inside the repository", "[vcs]")
{
	const fs::path root = fs::temp_directory_path() / "bernini_vcs_fence";

	SECTION("a path already inside comes back relative to the root")
	{
		const auto relative = editor::RepositoryRelativePath(root, root / "Data" / "a.bmesh");
		REQUIRE(relative.has_value());
		CHECK(*relative == QString("Data/a.bmesh"));
	}

	SECTION("a relative path is resolved against the root")
	{
		const auto relative = editor::RepositoryRelativePath(root, "Data/a.bmesh");
		REQUIRE(relative.has_value());
		CHECK(*relative == QString("Data/a.bmesh"));
	}

	SECTION("a path that climbs out is refused")
	{
		CHECK_FALSE(editor::RepositoryRelativePath(root, "../escaped.bmesh").has_value());
		CHECK_FALSE(editor::RepositoryRelativePath(root, "Data/../../escaped.bmesh").has_value());
	}

	SECTION("an absolute path somewhere else is refused")
	{
		const auto elsewhere = fs::temp_directory_path() / "bernini_vcs_elsewhere" / "a.bmesh";
		CHECK_FALSE(editor::RepositoryRelativePath(root, elsewhere).has_value());
	}

	SECTION("the root itself is not a path within the root")
	{
		CHECK_FALSE(editor::RepositoryRelativePath(root, root).has_value());
	}

#ifdef _WIN32
	// `D:foo` is drive-relative rather than absolute, so appending it would re-root the join.
	SECTION("a drive-relative path is refused")
	{
		CHECK_FALSE(editor::RepositoryRelativePath(root, "D:escaped.bmesh").has_value());
	}
#endif
}

TEST_CASE("Output far larger than a pipe buffer comes back whole", "[vcs]")
{
	const TempRepo repo("large_output");

	const QByteArray blob(4 * 1024 * 1024, 'x');
	WriteFile(repo.path / "large.bin", blob);

	const auto hashed = editor::RunGit(repo.path, { "hash-object", "-w" }, { "large.bin" });
	REQUIRE(hashed.Succeeded());

	const auto read =
		editor::RunGit(repo.path, { "cat-file", "blob", QString::fromUtf8(Trimmed(hashed)) });

	REQUIRE(read.Succeeded());
	CHECK(read.out.size() == blob.size());
}

// A git that asks for credentials with no window to ask in would block until the timeout, and take
// the loading screen with it. The alias runs the child's shell, which is what can see the variable.
TEST_CASE("The child is told never to prompt for credentials", "[vcs]")
{
	const TempRepo repo("no_prompt");

	const auto result = editor::RunGit(
		repo.path,
		{ "-c", "alias.showprompt=!echo \"$GIT_TERMINAL_PROMPT\"", "showprompt" });

	REQUIRE(result.Succeeded());
	CHECK(Trimmed(result) == QByteArray("0"));
}
