#pragma once

namespace test
{
	struct TrackedElement
	{
		static inline int g_Live          = 0;
		static inline int g_DoubleDestroy = 0;
		static inline int g_AssignToDead  = 0;

		static void
		ResetCounters() noexcept
		{
			g_Live          = 0;
			g_DoubleDestroy = 0;
			g_AssignToDead  = 0;
		}

		static constexpr uint32_t c_Alive = 0xA11EA11Eu;
		static constexpr uint32_t c_Dead  = 0xDEADDEADu;

		uint32_t magic = c_Alive;
		int      value = 0;

		TrackedElement() noexcept { ++g_Live; }
		explicit TrackedElement(int initialValue) noexcept : value(initialValue) { ++g_Live; }

		TrackedElement(const TrackedElement& other) noexcept : value(other.value) { ++g_Live; }
		TrackedElement(TrackedElement&& other) noexcept : value(other.value) { ++g_Live; }

		TrackedElement&
		operator=(const TrackedElement& other) noexcept
		{
			if (magic != c_Alive)
				++g_AssignToDead;
			value = other.value;
			return *this;
		}

		TrackedElement&
		operator=(TrackedElement&& other) noexcept
		{
			if (magic != c_Alive)
				++g_AssignToDead;
			value = other.value;
			return *this;
		}

		~TrackedElement() noexcept
		{
			if (magic != c_Alive)
				++g_DoubleDestroy;
			magic = c_Dead;
			--g_Live;
		}
	};

	// Every element the container created has been destroyed exactly once.
	inline bool
	TrackingClean() noexcept
	{
		return TrackedElement::g_Live == 0 && TrackedElement::g_DoubleDestroy == 0 &&
		       TrackedElement::g_AssignToDead == 0;
	}
}
