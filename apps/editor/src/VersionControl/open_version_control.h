#pragma once

#include "VersionControl/IVersionControl.h"

namespace editor
{
	/**
	 * The project's version control, or nothing when the project is not inside a repository.
	 *
	 * The one place that picks a backend. Everything above this holds an IVersionControl and cannot
	 * tell which one it got, which is what makes ADR-3's seam worth having.
	 *
	 * @param projectFile the project the user opened; the repository is discovered from its directory.
	 * @param dataDirectory the project's assets, for the reference guard.
	 */
	[[nodiscard]] std::unique_ptr<IVersionControl>
	OpenVersionControl(
		const std::filesystem::path& projectFile,
		const std::filesystem::path& dataDirectory);
}
