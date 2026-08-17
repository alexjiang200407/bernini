#include <core/platform/util.h>

#include <fcntl.h>
#include <mach-o/dyld.h>
#include <unistd.h>

namespace core
{
	std::string
	get_executable_name() noexcept
	{
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);

		std::string buffer(size, '\0');
		if (_NSGetExecutablePath(buffer.data(), &size) != 0)
			return {};

		return std::filesystem::path(buffer.c_str()).stem().string();
	}

	uint32_t
	process_id() noexcept
	{
		return static_cast<uint32_t>(::getpid());
	}

	namespace
	{
		bool
		sync_fd(const std::filesystem::path& path, int flags) noexcept
		{
			const int fd = ::open(path.c_str(), flags);
			if (fd < 0)
				return false;

			const int result = ::fsync(fd);
			::close(fd);
			return result == 0;
		}
	}

	bool
	sync_file(const std::filesystem::path& path) noexcept
	{
		return sync_fd(path, O_RDONLY);
	}

	bool
	sync_directory(const std::filesystem::path& directory) noexcept
	{
		return sync_fd(directory, O_RDONLY | O_DIRECTORY);
	}
}
