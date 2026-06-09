#pragma once

namespace core
{
	template <typename T>
	class SharedRef
	{
	public:
		typedef T InterfaceType;

		template <bool b, typename U = void>
		struct EnableIf
		{};

		template <typename U>
		struct EnableIf<true, U>
		{
			typedef U type;
		};

	protected:
		InterfaceType* ptr_;
		template <class U>
		friend class SharedRef;

		void
		InternalAddRef() const noexcept
		{
			if (ptr_ != nullptr)
			{
				ptr_->AddRef();
			}
		}

		unsigned long
		InternalRelease() noexcept
		{
			unsigned long ref  = 0;
			T*            temp = ptr_;

			if (temp != nullptr)
			{
				ptr_ = nullptr;
				ref  = temp->Release();
			}

			return ref;
		}

	public:
		SharedRef() noexcept : ptr_(nullptr) {}

		SharedRef(std::nullptr_t) noexcept : ptr_(nullptr) {}

		template <class U>
		SharedRef(U* other) noexcept : ptr_(other)
		{
			InternalAddRef();
		}

		SharedRef(const SharedRef& other) noexcept : ptr_(other.ptr_) { InternalAddRef(); }

		// copy ctor that allows to instanatiate class when U* is convertible to T*
		template <class U>
		SharedRef(
			const SharedRef<U>& other,
			typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* =
				nullptr) noexcept : ptr_(other.ptr_)

		{
			InternalAddRef();
		}

		SharedRef(SharedRef&& other) noexcept : ptr_(nullptr)
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
				nullptr) noexcept : ptr_(other.ptr_)
		{
			other.ptr_ = nullptr;
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
			if (ptr_ != other)
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
			if (ptr_ != other.ptr_)
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
			T* tmp = ptr_;
			ptr_   = r.ptr_;
			r.ptr_ = tmp;
		}

		void
		Swap(SharedRef& r) noexcept
		{
			T* tmp = ptr_;
			ptr_   = r.ptr_;
			r.ptr_ = tmp;
		}

		[[nodiscard]] T*
		Get() const noexcept
		{
			return ptr_;
		}

		operator T*() const { return ptr_; }

		InterfaceType*
		operator->() const noexcept
		{
			return ptr_;
		}

		T**
		operator&()  // NOLINT(google-runtime-operator)
		{
			return &ptr_;
		}

		[[nodiscard]] T* const*
		GetAddressOf() const noexcept
		{
			return &ptr_;
		}

		[[nodiscard]] T**
		GetAddressOf() noexcept
		{
			return &ptr_;
		}

		[[nodiscard]] T**
		ReleaseAndGetAddressOf() noexcept
		{
			InternalRelease();
			return &ptr_;
		}

		T*
		Detach() noexcept
		{
			T* ptr = ptr_;
			ptr_   = nullptr;
			return ptr;
		}

		// Set the pointer while keeping the object's reference count unchanged
		void
		Attach(InterfaceType* other)
		{
			if (ptr_ != nullptr)
			{
				auto ref = ptr_->Release();
				(void)ref;

				// Attaching to the same object only works if duplicate references are being coalesced. Otherwise
				// re-attaching will cause the pointer to be released and may cause a crash on a subsequent dereference.
				assert(ref != 0 || ptr_ != other);
			}

			ptr_ = other;
		}

		// Create a wrapper around a raw object while keeping the object's reference count unchanged
		static SharedRef<T>
		Create(T* other)
		{
			SharedRef<T> Ptr;
			Ptr.Attach(other);
			return Ptr;
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
			return ptr_ != nullptr;
		}

		template <std::derived_from<T> U>
		inline
		operator SharedRef<U>() const noexcept
		{
			U* castedPtr = static_cast<U*>(ptr_);
			if (castedPtr != nullptr)
			{
				castedPtr->AddRef();
				return SharedRef<U>::Create(castedPtr);
			}
			return SharedRef<U>(nullptr);
		}
	};  // SharedRef
}
