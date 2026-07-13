#include "fs_util.h"

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
}
