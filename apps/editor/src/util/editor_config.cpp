#include "util/editor_config.h"

#include <core/file/file.h>
#include <core/settings/Settings.h>
#include <exception>
#include <filesystem>

namespace editor
{
	std::filesystem::path
	DefaultConfigPath()
	{
		return core::file::get_executable_path().parent_path() / "config.json";
	}

	bool
	MemoryReportEnabled(const std::filesystem::path& configPath)
	{
		try
		{
			return core::Settings(configPath)["memoryReport"].GetOrDefault(true);
		}
		catch (const std::exception&)
		{
			return true;
		}
	}
}
