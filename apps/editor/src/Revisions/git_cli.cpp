#include "Revisions/git_cli.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>

namespace
{
	namespace fs = std::filesystem;

	/** How long a killed `git` is given to actually die before RunGit stops caring. */
	constexpr auto c_KillWaitMs = 2'000;

	QString
	ToQString(const fs::path& path)
	{
		return QString::fromStdWString(path.generic_wstring());
	}

	/**
	 * The absolute path to `git`, resolved once per run, or empty when there is none.
	 *
	 * Never the bare name: on Windows CreateProcess searches the working directory before PATH, and
	 * the working directory here is a project root full of files that arrived with the assets.
	 * QStandardPaths::findExecutable searches PATH and not the caller's directory.
	 */
	const QString&
	GitExecutable()
	{
		static const QString c_GitPath = QStandardPaths::findExecutable(QStringLiteral("git"));
		return c_GitPath;
	}

	/**
	 * The directory to run git in: `path` itself, or its parent when it names a file.
	 *
	 * A project is identified by its `.berniniproject` file, so the caller usually has a file in hand
	 * where git wants a directory.
	 */
	fs::path
	DirectoryOf(const fs::path& path)
	{
		std::error_code ec;
		if (fs::is_directory(path, ec))
		{
			return path;
		}
		return path.parent_path();
	}
}

namespace editor
{
	GitCommandResult
	RunGit(
		const fs::path&    workingDirectory,
		const QStringList& arguments,
		const QStringList& paths,
		int                timeoutMs)
	{
		if (GitExecutable().isEmpty())
		{
			return {};
		}

		QStringList full = arguments;
		if (!paths.isEmpty())
		{
			full << QStringLiteral("--") << paths;
		}

		auto environment = QProcessEnvironment::systemEnvironment();
		environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));

		QProcess process;
		process.setProgram(GitExecutable());
		process.setArguments(full);
		process.setWorkingDirectory(ToQString(workingDirectory));
		process.setProcessEnvironment(environment);
		QElapsedTimer clock;
		clock.start();
		process.start();

		// One budget across both waits, so a slow start cannot double the ceiling a caller reasoned
		// about; clamped because a negative wait is Qt's spelling of "forever".
		if (!process.waitForStarted(timeoutMs) ||
		    !process.waitForFinished(std::max(0, timeoutMs - static_cast<int>(clock.elapsed()))))
		{
			process.kill();
			process.waitForFinished(c_KillWaitMs);
			return { .out = process.readAllStandardOutput(),
				     .err = process.readAllStandardError() };
		}

		GitCommandResult result;
		result.out = process.readAllStandardOutput();
		result.err = process.readAllStandardError();

		// A git killed by a signal has no exit code worth reporting, and its 0 would read as success.
		if (process.exitStatus() == QProcess::NormalExit)
		{
			result.exitCode = process.exitCode();
		}
		return result;
	}

	std::optional<fs::path>
	FindRepositoryRoot(const fs::path& startingPoint)
	{
		const auto result = RunGit(
			DirectoryOf(startingPoint),
			{ QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel") });

		if (!result.Succeeded())
		{
			return std::nullopt;
		}

		const QString top = QString::fromUtf8(result.out).trimmed();
		if (top.isEmpty())
		{
			return std::nullopt;
		}
		return fs::path(top.toStdWString());
	}

	std::optional<QString>
	RepositoryRelativePath(const fs::path& repositoryRoot, const fs::path& path)
	{
		// A Windows drive-relative path (`D:foo`) is not absolute, and appending it to the root would
		// let it re-root the join rather than extend it.
		if (!path.is_absolute() && path.has_root_name())
		{
			return std::nullopt;
		}

		const fs::path resolved = path.is_absolute() ? path.lexically_normal() :
		                                               (repositoryRoot / path).lexically_normal();

		// "." is what lexically_relative returns for the root itself, which addresses the whole
		// repository rather than anything in it -- not what a caller naming one asset can have meant.
		const fs::path relative = resolved.lexically_relative(repositoryRoot.lexically_normal());
		if (relative.empty() || relative == "." || *relative.begin() == "..")
		{
			return std::nullopt;
		}
		return ToQString(relative);
	}
}
