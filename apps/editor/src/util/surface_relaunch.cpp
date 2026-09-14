#include "util/surface_relaunch.h"

#include <assetlib/Project.h>
#include <assetlib/project_layout.h>
#include <cstddef>
#include <filesystem>
#include <system_error>

namespace editor
{
	namespace
	{
		bool
		HoldsShaderSource(const std::filesystem::path& dir)
		{
			std::error_code ec;
			for (auto it = std::filesystem::directory_iterator(dir, ec);
			     !ec && it != std::filesystem::directory_iterator();
			     it.increment(ec))
			{
				if (it->is_regular_file(ec) && it->path().extension() == ".slang")
					return true;
			}
			return false;
		}

		// Two spellings of one directory -- a symlinked checkout's test project is the everyday one --
		// are the same shaders.
		bool
		IsSameDirectory(const std::filesystem::path& a, const std::filesystem::path& b)
		{
			if (a.empty() || b.empty())
				return a.empty() && b.empty();

			std::error_code ec;
			const bool      equivalent = std::filesystem::equivalent(a, b, ec);
			if (!ec)
				return equivalent;

			return a.lexically_normal() == b.lexically_normal();
		}
	}

	std::filesystem::path
	ShadersDirectoryOf(const std::filesystem::path& projectFile)
	{
		return assetlib::Project::DataDirectoryOf(projectFile) / assetlib::c_ShadersDirectoryName;
	}

	bool
	OpeningNeedsRelaunch(
		const std::filesystem::path& registeredShaders,
		std::size_t                  registeredSurfaces,
		const std::filesystem::path& projectShaders)
	{
		if (IsSameDirectory(registeredShaders, projectShaders))
			return false;

		return registeredSurfaces > 0 || HoldsShaderSource(projectShaders);
	}
}
