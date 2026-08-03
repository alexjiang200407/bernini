#pragma once

namespace core
{
	template <typename T>
	class SharedRef
	{
	public:
		typedef T InterfaceType;

		template <bool B, typename U = void>
		struct EnableIf
		{};

		template <typename U>
		struct EnableIf<true, U>
		{
			typedef U Type;
		};

	protected:
		InterfaceType* m_Ptr;
		template <class U>
		friend class SharedRef;

		void
		InternalAddRef() const noexcept
		{
			if (m_Ptr != nullptr)
			{
				m_Ptr->AddRef();
			}
		}

		unsigned long
		InternalRelease() noexcept
		{
			unsigned long ref  = 0;
			T*            temp = m_Ptr;

			if (temp != nullptr)
			{
				m_Ptr = nullptr;
				ref   = temp->Release();
			}

			return ref;
		}

	public:
		SharedRef() noexcept : m_Ptr(nullptr) {}

		SharedRef(std::nullptr_t) noexcept : m_Ptr(nullptr) {}

		template <class U>
		SharedRef(U* other) noexcept : m_Ptr(other)
		{
			InternalAddRef();
		}

		SharedRef(const SharedRef& other) noexcept : m_Ptr(other.m_Ptr) { InternalAddRef(); }

		// copy ctor that allows to instanatiate class when U* is convertible to T*
		template <class U>
		SharedRef(
			const SharedRef<U>& other,
			typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* =
				nullptr) noexcept : m_Ptr(other.m_Ptr)

		{
			InternalAddRef();
		}

		SharedRef(SharedRef&& other) noexcept : m_Ptr(nullptr)
		{
			if (this != reinterpret_cast<SharedRef*>(&reinterpret_cast<unsigned char&>(other)))
			{
				Swap(other);
			}
		}

		// Move ctor that allows instantiation of a class when U* is convertible to T*
		template <class U>
		SharedRef(
			SharedRef<U>&& other,
			typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* =
				nullptr) noexcept : m_Ptr(other.m_Ptr)
		{
			other.m_Ptr = nullptr;
		}

		~SharedRef() noexcept { InternalRelease(); }

		SharedRef&
		operator=(std::nullptr_t) noexcept
		{
			InternalRelease();
			return *this;
		}

		SharedRef&
		operator=(T* other) noexcept
		{
			if (m_Ptr != other)
			{
				SharedRef(other).Swap(*this);
			}
			return *this;
		}

		template <typename U>
		SharedRef&
		operator=(U* other) noexcept
		{
			SharedRef(other).Swap(*this);
			return *this;
		}

		SharedRef&
		operator=(const SharedRef& other) noexcept  // NOLINT(bugprone-unhandled-self-assignment)
		{
			if (m_Ptr != other.m_Ptr)
			{
				SharedRef(other).Swap(*this);
			}
			return *this;
		}

		template <class U>
		SharedRef&
		operator=(const SharedRef<U>& other) noexcept
		{
			SharedRef(other).Swap(*this);
			return *this;
		}

		SharedRef&
		operator=(SharedRef&& other) noexcept
		{
			SharedRef(static_cast<SharedRef&&>(other)).Swap(*this);
			return *this;
		}

		template <class U>
		SharedRef&
		operator=(SharedRef<U>&& other) noexcept
		{
			SharedRef(static_cast<SharedRef<U>&&>(other)).Swap(*this);
			return *this;
		}

		void
		Swap(SharedRef&& r) noexcept
		{
			T* tmp  = m_Ptr;
			m_Ptr   = r.m_Ptr;
			r.m_Ptr = tmp;
		}

		void
		Swap(SharedRef& r) noexcept
		{
			T* tmp  = m_Ptr;
			m_Ptr   = r.m_Ptr;
			r.m_Ptr = tmp;
		}

		[[nodiscard]] T*
		Get() const noexcept
		{
			return m_Ptr;
		}

		operator T*() const { return m_Ptr; }

		InterfaceType*
		operator->() const noexcept
		{
			return m_Ptr;
		}

		T**
		operator&()  // NOLINT(google-runtime-operator)
		{
			return &m_Ptr;
		}

		[[nodiscard]] T* const*
		GetAddressOf() const noexcept
		{
			return &m_Ptr;
		}

		[[nodiscard]] T**
		GetAddressOf() noexcept
		{
			return &m_Ptr;
		}

		[[nodiscard]] T**
		ReleaseAndGetAddressOf() noexcept
		{
			InternalRelease();
			return &m_Ptr;
		}

		T*
		Detach() noexcept
		{
			T* ptr = m_Ptr;
			m_Ptr  = nullptr;
			return ptr;
		}

		// Set the pointer while keeping the object's reference count unchanged
		void
		Attach(InterfaceType* other)
		{
			if (m_Ptr != nullptr)
			{
				auto ref = m_Ptr->Release();
				(void)ref;

				// Attaching to the same object only works if duplicate references are being coalesced. Otherwise
				// re-attaching will cause the pointer to be released and may cause a crash on a subsequent dereference.
				assert(ref != 0 || m_Ptr != other);
			}

			m_Ptr = other;
		}

		// Create a wrapper around a raw object while keeping the object's reference count unchanged
		static SharedRef<T>
		Create(T* other)
		{
			SharedRef<T> ptr;
			ptr.Attach(other);
			return ptr;
		}

		template <typename... Args>
		static SharedRef<T>
		Make(Args&&... args)
		{
			return SharedRef<T>::Create(new T(std::forward<Args>(args)...));
		}

		unsigned long
		Reset()
		{
			return InternalRelease();
		}

		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return m_Ptr != nullptr;
		}

		template <std::derived_from<T> U>
		inline
		operator SharedRef<U>() const noexcept
		{
			U* castedPtr = static_cast<U*>(m_Ptr);
			if (castedPtr != nullptr)
			{
				castedPtr->AddRef();
				return SharedRef<U>::Create(castedPtr);
			}
			return SharedRef<U>(nullptr);
		}
	};  // SharedRef
}
