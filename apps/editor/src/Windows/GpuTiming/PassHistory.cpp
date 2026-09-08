#include "Windows/GpuTiming/PassHistory.h"

#include <algorithm>
#include <bgl/PassTiming.h>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace editor
{
	PassHistory::PassHistory(std::size_t capacity) noexcept :
		m_Capacity(std::max<std::size_t>(capacity, 1))
	{}

	void
	PassHistory::Append(const bgl::PassTimings& timings)
	{
		if (timings.passes.empty() || timings.frame == m_LastFrame)
			return;

		m_LastFrame = timings.frame;
		MergePasses(timings.passes);

		Sample sample;
		sample.frame = timings.frame;
		sample.milliseconds.assign(m_Passes.size(), std::nullopt);

		for (const bgl::PassTiming& row : timings.passes)
		{
			const auto column = std::ranges::find(m_Passes, row.name);
			sample.milliseconds[static_cast<std::size_t>(std::distance(m_Passes.begin(), column))] =
				row.milliseconds;
		}

		m_Samples.emplace_back(std::move(sample));
		while (m_Samples.size() > m_Capacity)
		{
			m_Samples.pop_front();
		}
	}

	void
	PassHistory::Clear() noexcept
	{
		m_Passes.clear();
		m_Samples.clear();
		m_LastFrame = 0;
	}

	uint64_t
	PassHistory::FrameAt(std::size_t sample) const
	{
		return m_Samples[sample].frame;
	}

	std::optional<double>
	PassHistory::At(std::size_t sample, std::size_t pass) const
	{
		const std::vector<std::optional<double>>& row = m_Samples[sample].milliseconds;
		return pass < row.size() ? row[pass] : std::nullopt;
	}

	double
	PassHistory::TotalAt(std::size_t sample) const
	{
		double total = 0.0;
		for (const std::optional<double>& cell : m_Samples[sample].milliseconds)
		{
			total += cell.value_or(0.0);
		}
		return total;
	}

	double
	PassHistory::PeakTotal() const noexcept
	{
		double peak = 0.0;
		for (std::size_t sample = 0; sample < m_Samples.size(); ++sample)
		{
			peak = std::max(peak, TotalAt(sample));
		}
		return peak;
	}

	void
	PassHistory::MergePasses(const std::vector<bgl::PassTiming>& passes)
	{
		std::size_t after = 0;
		for (const bgl::PassTiming& row : passes)
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
}
