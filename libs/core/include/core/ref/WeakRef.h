#pragma once
#include <concepts>
#include <core/ref/SharedRef.h>

namespace core
{
	template <typename T>
	concept WeakRefTarget = requires(T& object) {
		{ object.TryAddRef() } -> std::same_as<bool>;
	};

	/**
	 * A reference that does not keep its object alive: Lock() gives a SharedRef while some other
	 * reference still holds the object, and null once the last has let go. What an index of
	 * ref-counted objects holds, so the index can find an object without owning it.
	 *
	 * Unlike std::weak_ptr there is no control block, so the object's storage goes with it. A
	 * WeakRef is only for a holder that hears of the destruction before the storage is freed and
	 * serializes Lock() against it -- an index whose entry the object's destructor erases under the
	 * lock Lock() is called under.
	 */
	template <WeakRefTarget T>
	class WeakRef
	{
	public:
		WeakRef() noexcept = default;

		explicit WeakRef(T* object) noexcept : m_Object(object) {}

		/**
		 * A reference to the object, or null when its count has reached zero and it is being
		 * destroyed.
		 *
		 * @pre the object's destructor has not finished: the holder has not yet been told of it.
		 */
		[[nodiscard]] SharedRef<T>
		Lock() const noexcept
		{
			if (m_Object == nullptr || !m_Object->TryAddRef())
				return nullptr;
			auto reference = SharedRef<T>(m_Object);
			m_Object->Release();
			return reference;
		}

	private:
		T* m_Object = nullptr;
	};
}
