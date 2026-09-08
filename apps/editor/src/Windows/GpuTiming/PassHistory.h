#pragma once

#include <bgl/PassTiming.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace editor
{
	/**
	 * The last N timed frames of a viewport, as a table of passes against frames.
	 *
	 * A frame graph does not run the same passes every frame -- a culled pass leaves no row, and
	 * turning TAA off removes two -- so the passes are a union in execution order, and a cell is
	 * empty where that pass did not run in that frame. Empty is not zero: zero is a pass that ran
	 * and could not be sampled, which bgl::PassTiming already reports as such.
	 */
	class PassHistory
	{
	public:
		// 10 seconds at 60Hz: long enough to scroll back to the spike that made someone open the
		// window, short enough that the whole table is one screen wide at a pixel a sample.
		static constexpr std::size_t c_DefaultCapacity = 600;

		explicit PassHistory(std::size_t capacity = c_DefaultCapacity) noexcept;

		/**
		 * Records one timed frame, oldest samples dropped once the capacity is reached. A frame
		 * whose id is the one last recorded is ignored, so a caller that reads faster than the GPU
		 * resolves does not plot the same frame twice.
		 */
		void
		Append(const bgl::PassTimings& timings);

		void
		Clear() noexcept;

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
		FrameAt(std::size_t sample) const;

		/**
		 * What `pass` cost in `sample`, or nullopt where that frame did not run it.
		 *
		 * @pre `sample` < SampleCount() and `pass` < Passes().size().
		 */
		[[nodiscard]] std::optional<double>
		At(std::size_t sample, std::size_t pass) const;

		/** @pre `sample` < SampleCount(). */
		[[nodiscard]] double
		TotalAt(std::size_t sample) const;

		/** The largest frame total held, which is what a chart scales its axis to. Zero when empty. */
		[[nodiscard]] double
		PeakTotal() const noexcept;

	private:
		// Merges a frame's pass names into m_Passes, keeping execution order: a pass first seen part
		// way through the history belongs beside the pass it ran after, not at the end of the table.
		void
		MergePasses(const std::vector<bgl::PassTiming>& passes);

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
