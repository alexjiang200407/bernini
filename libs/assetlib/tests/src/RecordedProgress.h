#pragma once
#include <algorithm>
#include <assetlib/progress.h>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace assetlib::test
{
	/**
	 * One reported step, with its subject **owned**. A `ProgressEvent`'s is a view into whatever
	 * the cook was holding at the time, and none of it outlives the call.
	 */
	struct RecordedStep
	{
		ProgressPhase phase = ProgressPhase::kScanning;
		std::string   subject;
		size_t        done  = 0;
		size_t        total = 0;
	};

	/**
	 * Every step a cook reported, in the order the sink was told about them. The order is the
	 * thing worth asserting on: a cook that fans out still has to finish one stage before the next
	 * begins, and that is invisible in the report it returns.
	 */
	struct RecordedProgress
	{
		std::vector<RecordedStep> events;

		[[nodiscard]] ProgressSink
		Sink()
		{
			return [this](const ProgressEvent& event) {
				events.push_back(
					RecordedStep{ event.phase,
				                  std::string(event.subject),
				                  event.done,
				                  event.total });
			};
		}

		/** The steps of one phase, in order. */
		[[nodiscard]] std::vector<RecordedStep>
		Of(ProgressPhase phase) const
		{
			auto matching = std::vector<RecordedStep>();
			for (const RecordedStep& step : events)
				if (step.phase == phase)
					matching.push_back(step);
			return matching;
		}

		/** The index of the first step whose subject ends in `extension`, or npos for none. */
		[[nodiscard]] static size_t
		FirstOf(std::span<const RecordedStep> steps, std::string_view extension)
		{
			for (size_t i = 0; i < steps.size(); ++i)
				if (steps[i].subject.ends_with(extension))
					return i;
			return std::string::npos;
		}

		/** The index of the last such step, or npos for none. */
		[[nodiscard]] static size_t
		LastOf(std::span<const RecordedStep> steps, std::string_view extension)
		{
			for (size_t i = steps.size(); i-- > 0;)
				if (steps[i].subject.ends_with(extension))
					return i;
			return std::string::npos;
		}

		[[nodiscard]] static size_t
		CountOf(std::span<const RecordedStep> steps, std::string_view extension)
		{
			return static_cast<size_t>(
				std::ranges::count_if(steps, [extension](const RecordedStep& step) {
					return step.subject.ends_with(extension);
				}));
		}
	};
}
