#include "fs_util.h"

#include <core/file/file.h>

namespace assetlib
{
	void
	createDirectories(const std::filesystem::path& dir)
	{
		// create_directories returns false without setting `ec` when the directory was already there,
		// which is a success. Only `ec` distinguishes that from a real failure.
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);

		if (ec)
			throw std::runtime_error(
				"assetlib: cannot create directory '" + dir.string() + "': " + ec.message());
	}

	std::optional<std::filesystem::file_time_type>
	mtimeOf(const std::filesystem::path& path)
	{
		std::error_code ec;

		const auto written = std::filesystem::last_write_time(path, ec);
		if (ec)
			return std::nullopt;

		return written;
	}

	std::string
	fileErrorMessage(std::string_view what, const std::filesystem::path& path)
	{
		auto message = std::string(what) + " '" + path.string() + "'";

		// The CRT maps the Win32 error onto errno, so a locked or read-only file surfaces as EACCES
		// rather than as a bare "the stream failed".
		if (errno != 0)
			message += ": " + std::generic_category().message(errno);

		return message;
	}

	void
	writeFileBytes(
		const std::filesystem::path& path,
		std::span<const std::byte>   bytes,
		std::string_view             what)
	{
		try
		{
			core::file::write_atomic(path, bytes);
		}
		catch (const std::exception& error)
		{
			throw std::runtime_error(std::string(what) + ": " + error.what());
		}
	}
}
