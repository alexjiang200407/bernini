#include "VersionControl/open_version_control.h"

#include "VersionControl/GitVersionControl.h"
#include "VersionControl/git_cli.h"

namespace editor
{
	std::unique_ptr<IVersionControl>
	OpenVersionControl(
		const std::filesystem::path& projectFile,
		const std::filesystem::path& dataDirectory)
	{
		const auto root = FindRepositoryRoot(projectFile);
		if (!root.has_value())
		{
			return nullptr;
		}
		return std::make_unique<GitVersionControl>(*root, dataDirectory);
	}
}
