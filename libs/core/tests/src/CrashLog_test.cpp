#include <core/err/util.h>

// The crash log is written from a signal handler, so the only way to exercise it is to crash a
// process: every case here forks, faults the child, and reads back what it left behind.
#if !defined(_WIN32)
#	include <sys/wait.h>
#	include <unistd.h>

namespace
{
	constexpr uintptr_t c_PoisonedPointer = 0x5555555555555555ull;

	// noinline, so the fault has a frame of its own to be named by. The trace itself cannot name it:
	// a frame-pointer walk reads return addresses, and this function never returns.
	__attribute__((noinline)) void
	FaultOnAPoisonedPointer()
	{
		volatile const int* pointer      = reinterpret_cast<volatile const int*>(c_PoisonedPointer);
		[[maybe_unused]] const int value = *pointer;
	}

	// A directory of its own, so the newest crash log in it is unambiguously the one just written.
	struct CrashDirectory
	{
		std::filesystem::path path;

		explicit CrashDirectory(const char* name) :
			path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}

		~CrashDirectory()
		{
			std::error_code ec;
			std::filesystem::remove_all(path, ec);
		}

		[[nodiscard]] std::string
		ReadOnlyLog() const
		{
			for (const auto& entry : std::filesystem::directory_iterator(path))
			{
				if (entry.path().extension() == ".log")
				{
					std::ifstream     file(entry.path());
					std::stringstream contents;
					contents << file.rdbuf();
					return contents.str();
				}
			}

			return {};
		}
	};

	// Runs `crash` in a forked child inside `directory`, and returns the log it left. The child
	// never returns: the handler leaves through _Exit, so Catch2 cannot see two processes running
	// its session.
	std::string
	CrashLogOfAChild(const CrashDirectory& directory, void (*crash)())
	{
		const pid_t child = fork();
		REQUIRE(child >= 0);

		if (child == 0)
		{
			// The log is written relative to the working directory, which is what puts it in `path`.
			if (chdir(directory.path.c_str()) != 0)
				_exit(2);

			core::install_crash_handlers();
			crash();
			_exit(3);
		}

		int status = 0;
		REQUIRE(waitpid(child, &status, 0) == child);

		return directory.ReadOnlyLog();
	}
}

TEST_CASE("A crash log names the address that faulted", "[crashlog]")
{
	const CrashDirectory directory("bernini_crashlog_address");
	const std::string    log = CrashLogOfAChild(directory, FaultOnAPoisonedPointer);

	REQUIRE_FALSE(log.empty());
	REQUIRE(log.find("signal 11") != std::string::npos);
	REQUIRE(log.find("0x5555555555555555") != std::string::npos);
}

TEST_CASE("A crash log names the function that faulted", "[crashlog]")
{
	const CrashDirectory directory("bernini_crashlog_function");
	const std::string    log = CrashLogOfAChild(directory, FaultOnAPoisonedPointer);

	// The whole point: the faulting function is the one frame a walk of return addresses cannot
	// reach, so without the header line the log would name its caller and nothing else.
	REQUIRE(log.find("FaultOnAPoisonedPointer") != std::string::npos);
}

TEST_CASE("A raised signal is logged without a faulting address", "[crashlog]")
{
	const CrashDirectory directory("bernini_crashlog_raised");
	const std::string    log = CrashLogOfAChild(directory, [] { std::abort(); });

	REQUIRE(log.find("signal 6") != std::string::npos);

	// abort() leaves si_addr holding the pid that raised it, not an address. Reporting that would
	// be the same misdirection the header line exists to end.
	REQUIRE(log.find("faulting address") == std::string::npos);
}
#endif
