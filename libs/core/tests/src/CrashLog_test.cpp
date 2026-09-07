#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/err/util.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

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

		[[nodiscard]] std::filesystem::path
		OnlyLogPath() const
		{
			for (const auto& entry : std::filesystem::directory_iterator(path))
				if (entry.path().extension() == ".log")
					return entry.path();
			return {};
		}

		[[nodiscard]] std::string
		ReadOnlyLog() const
		{
			const std::filesystem::path log = OnlyLogPath();
			if (log.empty())
				return {};

			std::ifstream     file(log);
			std::stringstream contents;
			contents << file.rdbuf();
			return contents.str();
		}
	};

	// Runs `crash` in a forked child inside `directory`, and returns the log it left. The child
	// never returns: the handler leaves through _Exit, so Catch2 cannot see two processes running
	// its session.
	std::string
	CrashLogOfAChild(const CrashDirectory& directory, void (*crash)())
	{
		// Installed in the parent, as every app does first thing in main: the child inherits the
		// dispositions and the time-zone offset across the fork, and calls no time function at all.
		// Installing in the child would make the process lineage's first localtime call there,
		// which is the hang docs/known_issues.md records.
		core::install_crash_handlers();

		const pid_t child = fork();
		REQUIRE(child >= 0);

		if (child == 0)
		{
			// The log is written relative to the working directory, which is what puts it in `path`.
			if (chdir(directory.path.c_str()) != 0)
				_exit(2);

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

// The stamp is computed by the handler from time() and arithmetic, never by localtime or
// strftime: neither is safe under a signal, and on macOS the first localtime in a forked child
// waits forever on the time zone's dispatch_once (docs/known_issues.md). So the arithmetic is
// checked against the wall clock the parent reads the ordinary way.
TEST_CASE("A crash log is stamped with the local wall clock", "[crashlog]")
{
	const CrashDirectory directory("bernini_crashlog_stamp");

	const std::time_t before = std::time(nullptr);
	const std::string log    = CrashLogOfAChild(directory, [] { std::abort(); });
	const std::time_t after  = std::time(nullptr);
	REQUIRE_FALSE(log.empty());

	const std::string name = directory.OnlyLogPath().filename().string();
	INFO("log name: " << name);
	const size_t at = name.find("_crash_");
	REQUIRE(at != std::string::npos);
	const std::string stamp = name.substr(at + 7, 15);
	REQUIRE(stamp.size() == 15);
	REQUIRE(stamp[8] == '_');

	const auto stampOf = [](std::time_t t) {
		std::tm local = {};
		localtime_r(&t, &local);
		char buffer[32] = {};
		std::snprintf(
			buffer,
			sizeof(buffer),
			"%04d%02d%02d_%02d%02d%02d",
			local.tm_year + 1900,
			local.tm_mon + 1,
			local.tm_mday,
			local.tm_hour,
			local.tm_min,
			local.tm_sec);
		return std::string(buffer);
	};

	// The child crashed somewhere between the two readings, so its stamp lies between theirs.
	CHECK(stamp >= stampOf(before));
	CHECK(stamp <= stampOf(after));
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
