#pragma once

#include <cstddef>
#include <string>
#include <vector>
namespace assetlib
{
	/** What Reimport did for one copied source. */
	struct ReimportedSource
	{
		// The copied source itself -- `Authored/Meshes/<name>.glb`, not the `.bimport` beside it.
		std::string source;

		// The entries of the document's `outputs` this run put back, sorted. Empty when every one of
		// them was already on disk -- which is what a settled project reports for every source.
		std::vector<std::string> written;

		// Set when the source could not be re-imported. `written` still holds whatever landed
		// before it threw -- there is no rollback, and those files are on disk.
		std::string message;
	};

	struct ReimportReport
	{
		std::vector<ReimportedSource> sources;

		/** Outputs written across every source. */
		[[nodiscard]] size_t
		GetWrittenCount() const noexcept;

		[[nodiscard]] size_t
		GetFailedCount() const noexcept;
	};
}
