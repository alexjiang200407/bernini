#include "progress_report.h"
#include <assetlib/progress.h>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace assetlib
{
	ProgressSink
	serialized(ProgressSink sink)
	{
		if (!sink)
			return {};

		auto guard = std::make_shared<std::mutex>();
		return [sink = std::move(sink), guard](const ProgressEvent& event) {
			const auto held = std::lock_guard(*guard);
			sink(event);
		};
	}

	void
	reportStep(
		const ProgressSink& sink,
		ProgressPhase       phase,
		std::string_view    subject,
		size_t              done,
		size_t              total)
	{
		if (sink)
			sink(ProgressEvent{ phase, subject, done, total });
	}
}
