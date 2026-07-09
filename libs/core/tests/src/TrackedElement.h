#pragma once

namespace test
{
	struct TrackedElement
	{
		static inline int s_Live          = 0;
		static inline int s_DoubleDestroy = 0;
		static inline int s_AssignToDead  = 0;

		static void
		ResetCounters() noexcept
		{
			s_Live          = 0;
			s_DoubleDestroy = 0;
			s_AssignToDead  = 0;
		}

		static constexpr uint32_t c_Alive = 0xA11EA11Eu;
		static constexpr uint32_t c_Dead  = 0xDEADDEADu;

		uint32_t magic = c_Alive;
		int      value = 0;

		TrackedElement() noexcept { ++s_Live; }
		explicit TrackedElement(int a_value) noexcept : value(a_value) { ++s_Live; }

		TrackedElement(const TrackedElement& a_other) noexcept : value(a_other.value) { ++s_Live; }
		TrackedElement(TrackedElement&& a_other) noexcept : value(a_other.value) { ++s_Live; }

		TrackedElement&
		operator=(const TrackedElement& a_other) noexcept
		{
			if (magic != c_Alive)
				++s_AssignToDead;
			value = a_other.value;
			return *this;
		}

		TrackedElement&
		operator=(TrackedElement&& a_other) noexcept
		{
			if (magic != c_Alive)
				++s_AssignToDead;
			value = a_other.value;
			return *this;
		}

		~TrackedElement() noexcept
		{
			if (magic != c_Alive)
				++s_DoubleDestroy;
			magic = c_Dead;
			--s_Live;
		}
	};

	// Every element the container created has been destroyed exactly once.
	inline bool
	TrackingClean() noexcept
	{
		return TrackedElement::s_Live == 0 && TrackedElement::s_DoubleDestroy == 0 &&
		       TrackedElement::s_AssignToDead == 0;
	}
}
