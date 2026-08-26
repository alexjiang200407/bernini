#pragma once

namespace assetlib
{
	/** What Reimport did for one copied source. */
	struct ReimportedSource
	{
		std::string source;  // the copied source's mount key

		// The outputs produced, sorted. Empty when every one of them was already current -- which
		// is what a second run of a settled project reports for every source.
		std::vector<std::string> written;

		std::string
			message;  // set when the source could not be re-imported; `written` is then empty
	};

	struct ReimportReport
	{
		std::vector<ReimportedSource> sources;

		/** Outputs written across every source. */
		[[nodiscard]] size_t
		WrittenCount() const noexcept;

		[[nodiscard]] size_t
		FailedCount() const noexcept;
	};
}
