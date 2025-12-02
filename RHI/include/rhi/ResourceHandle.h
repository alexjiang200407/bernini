#pragma once

namespace rhi
{
	template <typename T>
	//requires std::derived_from<T, IResource>
	class ResourceHandle
	{
	public:
		using InterfaceType = T;

	protected:
		InterfaceType* ptr = nullptr;

		void
		InternalAddRef() const noexcept
		{
			if (ptr)
				ptr->AddRef();
		}

		unsigned long
		InternalRelease() noexcept
		{
			unsigned long ref = 0;
			if (ptr)
			{
				T* temp = ptr;
				ptr     = nullptr;
				ref     = temp->Release();
			}
			return ref;
		}

	public:
		ResourceHandle() noexcept = default;
		ResourceHandle(std::nullptr_t) noexcept : ptr(nullptr) {}

		explicit ResourceHandle(T* ptr) noexcept : ptr(ptr) { InternalAddRef(); }

		ResourceHandle(const ResourceHandle& other) noexcept : ptr(other.ptr) { InternalAddRef(); }

		template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
		ResourceHandle(const ResourceHandle<U>& other) noexcept : ptr(other.Get())
		{
			InternalAddRef();
		}

		ResourceHandle(ResourceHandle&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }

		template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
		ResourceHandle(ResourceHandle<U>&& other) noexcept : ptr(other.Get())
		{
			other.Detach();
		}

		~ResourceHandle() noexcept { InternalRelease(); }

		ResourceHandle&
		operator=(std::nullptr_t) noexcept
		{
			InternalRelease();
			return *this;
		}

		ResourceHandle&
		operator=(T* ptr) noexcept
		{
			ResourceHandle(ptr).Swap(*this);
			return *this;
		}

		template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
		ResourceHandle&
		operator=(U* ptr) noexcept
		{
			ResourceHandle(ptr).Swap(*this);
			return *this;
		}

		ResourceHandle&
		operator=(const ResourceHandle& other) noexcept
		{
			ResourceHandle(other).Swap(*this);
			return *this;
		}

		template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
		ResourceHandle&
		operator=(const ResourceHandle<U>& other) noexcept
		{
			ResourceHandle(other).Swap(*this);
			return *this;
		}

		ResourceHandle&
		operator=(ResourceHandle&& other) noexcept
		{
			ResourceHandle(std::move(other)).Swap(*this);
			return *this;
		}

		template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
		ResourceHandle&
		operator=(ResourceHandle<U>&& other) noexcept
		{
			ResourceHandle(std::move(other)).Swap(*this);
			return *this;
		}

		// Swap
		void
		Swap(ResourceHandle& other) noexcept
		{
			std::swap(ptr, other.ptr);
		}

		// Accessors
		[[nodiscard]] T*
		Get() const noexcept
		{
			return ptr;
		}

		T*
		operator->() const noexcept
		{
			return ptr;
		}

		[[nodiscard]] T**
		GetAddressOf() noexcept
		{
			return &ptr;
		}

		[[nodiscard]] T**
		ReleaseAndGetAddressOf() noexcept
		{
			InternalRelease();
			return &ptr;
		}

		void
		Attach(T* other)
		{
			InternalRelease();
			ptr = other;
		}

		unsigned long
		Reset() noexcept
		{
			return InternalRelease();
		}

		ResourceHandle*
		operator&() = delete;
	};
}
