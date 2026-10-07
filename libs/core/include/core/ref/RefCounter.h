#pragma once
#include <atomic>
#include <concepts>
#include <core/ref/Ref.h>

namespace core
{
	template <class T>
	concept RefCounterConcept = std::derived_from<T, Ref>;

	template <RefCounterConcept T>
	class RefCounter : public T
	{
	public:
		RefCounter() = default;

		RefCounter(const RefCounter&) = delete;
		RefCounter(RefCounter&&)      = delete;

		RefCounter&
		operator=(const RefCounter&) = delete;

		RefCounter&
		operator=(RefCounter&&) = delete;

		unsigned long
		AddRef()
		{
			return ++m_RefCount;
		}

		/**
		 * A reference added only while one is still held: false once the count has reached zero,
		 * when the object is being destroyed. What a registry that finds objects by a raw pointer it
		 * does not own takes, since the last Release may race the lookup.
		 */
		[[nodiscard]] bool
		TryAddRef() noexcept
		{
			unsigned long count = m_RefCount.load();
			while (count != 0)
			{
				if (m_RefCount.compare_exchange_weak(count, count + 1))
					return true;
			}
			return false;
		}

		unsigned long
		Release()
		{
			unsigned long result = --m_RefCount;
			if (result == 0)
			{
				delete this;
			}
			return result;
		}

		unsigned long
		GetRefCount()
		{
			return m_RefCount.load();
		}

	private:
		std::atomic<unsigned long> m_RefCount = 1;
	};
}
