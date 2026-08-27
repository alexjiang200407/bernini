#pragma once

namespace assetlib
{
	/** What Reimport did for one copied source. */
	struct ReimportedSource
	{
		// The copied source itself -- `meshes_src/<name>.glb`, not the `.bimport` beside it.
		std::string source;

		// The entries of the document's `outputs` this run put back, sorted. Empty when every one of
		// them was already on disk -- which is what a settled project reports for every source.
		std::vector<std::string> written;

		std::string
			message;  // set when the source could not be re-imported; `written` is then empty
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
