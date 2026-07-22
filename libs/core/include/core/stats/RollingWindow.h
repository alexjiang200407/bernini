#pragma once

namespace core
{
	/**
	 * A fixed-capacity ring of the last `N` samples, for summarising a live signal such as
	 * frame time. Overwrites the oldest sample once full; never allocates.
	 *
	 * Statistics are computed on demand rather than maintained incrementally, so a query is
	 * O(N) while `Push` is O(1). This suits a signal sampled far more often than it is read
	 * -- pushed every frame, read when a readout refreshes.
	 */
	template <std::size_t N>
		requires(N > 0)
	class RollingWindow
	{
	public:
		void
		Push(double sample) noexcept
		{
			m_Samples[m_Head] = sample;
			m_Head            = (m_Head + 1) % N;
			if (m_Size < N)
				++m_Size;
		}

		/** Zero when empty. */
		[[nodiscard]] double
		Mean() const noexcept
		{
			if (m_Size == 0)
				return 0.0;

			auto sum = 0.0;
			for (std::size_t i = 0; i < m_Size; ++i)
			{
				sum += m_Samples[i];
			}
			return sum / static_cast<double>(m_Size);
		}

		/** Zero when empty. */
		[[nodiscard]] double
		Max() const noexcept
		{
			if (m_Size == 0)
				return 0.0;

			return *std::ranges::max_element(std::span(m_Samples.data(), m_Size));
		}

		/** Samples counted, saturating at the capacity. */
		[[nodiscard]] std::size_t
		Size() const noexcept
		{
			return m_Size;
		}

		[[nodiscard]] static constexpr std::size_t
		Capacity() noexcept
		{
			return N;
		}

		void
		Reset() noexcept
		{
			m_Size = 0;
			m_Head = 0;
		}

	private:
		std::array<double, N> m_Samples{};
		std::size_t           m_Size = 0;
		std::size_t           m_Head = 0;
	};
}
