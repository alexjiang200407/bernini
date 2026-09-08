#pragma once
#include <algorithm>
#include <bgl/PassTiming.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace bgl
{
	/**
	 * The last N timed frames of one render target, as a table of passes against frames: what a
	 * caller polling IGraphics::GetPassTimings accumulates before it charts, exports or summarises.
	 *
	 * A frame graph does not run the same passes every frame -- a culled pass leaves no row, and
	 * turning TAA off removes two -- so the passes are a union in execution order, and a cell is
	 * empty where that pass did not run in that frame. Empty is not zero: zero is a pass that ran
	 * and could not be sampled, which PassTiming already reports as such.
	 */
	class PassHistory
	{
	public:
		// 10 seconds at 60Hz: long enough to scroll back to the spike that made someone open the
		// window, short enough that the whole table is one screen wide at a pixel a sample.
		static constexpr std::size_t c_DefaultCapacity = 600;

		explicit PassHistory(std::size_t capacity = c_DefaultCapacity) noexcept :
			m_Capacity(std::max<std::size_t>(capacity, 1))
		{}

		/**
		 * Records one timed frame, oldest samples dropped once the capacity is reached. A frame
		 * whose id is the one last recorded is ignored, so a caller that reads faster than the GPU
		 * resolves does not record the same frame twice.
		 */
		void
		Append(const PassTimings& timings)
		{
			if (timings.passes.empty() || timings.frame == m_LastFrame)
				return;

			m_LastFrame = timings.frame;
			MergePasses(timings.passes);

			Sample sample;
			sample.frame = timings.frame;
			sample.milliseconds.assign(m_Passes.size(), std::nullopt);

			for (const PassTiming& row : timings.passes)
			{
				const auto column = std::ranges::find(m_Passes, row.name);
				sample.milliseconds[static_cast<std::size_t>(
					std::distance(m_Passes.begin(), column))] = row.milliseconds;
			}

			m_Samples.emplace_back(std::move(sample));
			while (m_Samples.size() > m_Capacity)
			{
				m_Samples.pop_front();
			}
		}

		void
		Clear() noexcept
		{
			m_Passes.clear();
			m_Samples.clear();
			m_LastFrame = 0;
		}

		/** The passes, in the order the frames ran them; the columns of the table. */
		[[nodiscard]] std::span<const std::string>
		Passes() const noexcept
		{
			return m_Passes;
		}

		[[nodiscard]] std::size_t
		SampleCount() const noexcept
		{
			return m_Samples.size();
		}

		[[nodiscard]] std::size_t
		Capacity() const noexcept
		{
			return m_Capacity;
		}

		/** @pre `sample` < SampleCount(). */
		[[nodiscard]] uint64_t
		FrameAt(std::size_t sample) const
		{
			return m_Samples[sample].frame;
		}

		/**
		 * What `pass` cost in `sample`, or nullopt where that frame did not run it.
		 *
		 * @pre `sample` < SampleCount() and `pass` < Passes().size().
		 */
		[[nodiscard]] std::optional<double>
		At(std::size_t sample, std::size_t pass) const
		{
			const std::vector<std::optional<double>>& row = m_Samples[sample].milliseconds;
			return pass < row.size() ? row[pass] : std::nullopt;
		}

		/** @pre `sample` < SampleCount(). */
		[[nodiscard]] double
		TotalAt(std::size_t sample) const
		{
			double total = 0.0;
			for (const std::optional<double>& cell : m_Samples[sample].milliseconds)
			{
				total += cell.value_or(0.0);
			}
			return total;
		}

		/** The largest frame total held, which is what a chart scales its axis to. Zero when empty. */
		[[nodiscard]] double
		PeakTotal() const noexcept
		{
			double peak = 0.0;
			for (std::size_t sample = 0; sample < m_Samples.size(); ++sample)
			{
				peak = std::max(peak, TotalAt(sample));
			}
			return peak;
		}

	private:
		// Merges a frame's pass names into m_Passes, keeping execution order: a pass first seen part
		// way through the history belongs beside the pass it ran after, not at the end of the table.
		void
		MergePasses(const std::vector<PassTiming>& passes)
		{
			std::size_t after = 0;
			for (const PassTiming& row : passes)
			{
				const auto known = std::ranges::find(m_Passes, row.name);
				if (known != m_Passes.end())
				{
					after = static_cast<std::size_t>(std::distance(m_Passes.begin(), known)) + 1;
					continue;
				}

				m_Passes.insert(m_Passes.begin() + static_cast<std::ptrdiff_t>(after), row.name);

				// The samples already recorded are indexed by the old column order, so each of them
				// gains an empty cell where the new pass went -- it did not run in any of them.
				for (Sample& sample : m_Samples)
				{
					if (after < sample.milliseconds.size())
					{
						sample.milliseconds.insert(
							sample.milliseconds.begin() + static_cast<std::ptrdiff_t>(after),
							std::nullopt);
					}
				}

				++after;
			}
		}

		struct Sample
		{
			uint64_t frame = 0;

			// Indexed like m_Passes and free to be shorter than it: a pass appended to the table
			// after this frame was recorded did not run in it.
			std::vector<std::optional<double>> milliseconds;
		};

		std::size_t              m_Capacity;
		std::vector<std::string> m_Passes;
		std::deque<Sample>       m_Samples;
		uint64_t                 m_LastFrame = 0;
	};
}
