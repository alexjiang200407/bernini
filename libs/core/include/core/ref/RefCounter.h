#pragma once
#include <core/ref/Ref.h>
#include <core/type_traits.h>

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
