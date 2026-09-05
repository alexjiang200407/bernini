#pragma once
#include <assetlib/progress.h>
#include <cstddef>
#include <string_view>

namespace assetlib
{
	/**
	 * `sink` behind a lock of its own, for a cook that reports from several threads. The one place
	 * ProgressSink's serialization rule is kept, so no sink and no threaded cook keeps it twice.
	 *
	 * An empty sink stays empty: there is nothing to serialize, and a lock per step is not free.
	 */
	[[nodiscard]] ProgressSink
	serialized(ProgressSink sink);

	/** Reports one step to `sink` if there is one. */
	void
	reportStep(
		const ProgressSink& sink,
		ProgressPhase       phase,
		std::string_view    subject,
		size_t              done  = 0,
		size_t              total = 0);
}
