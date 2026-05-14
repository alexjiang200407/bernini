#pragma once

namespace core
{
	template <typename ImplType>
	class RefCountPImpl
	{
	protected:
		RefCountPImpl() noexcept = default;

		RefCountPImpl(ImplType* impl) noexcept : m_Impl(impl)
		{
			if (m_Impl)
			{
				m_RefCount = new size_t(1);
			}
		}

		template <typename... Args>
		RefCountPImpl(Args&&... args) :
			m_Impl(new ImplType(std::forward<Args>(args)...)), m_RefCount(new uint32_t(1))
		{}

		~RefCountPImpl() { Release(); }

		RefCountPImpl(const RefCountPImpl& other) noexcept :
			m_Impl(other.m_Impl), m_RefCount(other.m_RefCount)
		{
			Retain();
		}

		RefCountPImpl&
		operator=(const RefCountPImpl& other) noexcept
		{
			if (this != &other)
			{
				Release();
				m_Impl     = other.m_Impl;
				m_RefCount = other.m_RefCount;
				Retain();
			}
			return *this;
		}

		RefCountPImpl(RefCountPImpl&& other) noexcept :
			m_Impl(other.m_Impl), m_RefCount(other.m_RefCount)
		{
			other.m_Impl     = nullptr;
			other.m_RefCount = nullptr;
		}

		RefCountPImpl&
		operator=(RefCountPImpl&& other) noexcept
		{
			if (this != &other)
			{
				Release();
				m_Impl     = other.m_Impl;
				m_RefCount = other.m_RefCount;

				other.m_Impl     = nullptr;
				other.m_RefCount = nullptr;
			}
			return *this;
		}

		uint32_t
		GetRefCount() const noexcept
		{
			return m_RefCount ? *m_RefCount : 0;
		}

		template <typename... Args>
		void
		EmplaceImpl(Args&&... args)
		{
			Release();
			m_Impl     = new ImplType(std::forward<Args>(args)...);
			m_RefCount = new size_t(1);
		}

	public:
		ImplType*
		operator->() noexcept
		{
			return m_Impl;
		}
		const ImplType*
		operator->() const noexcept
		{
			return m_Impl;
		}

		ImplType&
		operator*() noexcept
		{
			return *m_Impl;
		}
		const ImplType&
		operator*() const noexcept
		{
			return *m_Impl;
		}

		ImplType*
		GetImpl() const noexcept
		{
			return m_Impl;
		}

		[[nodiscard]]
		bool
		IsInitialized() const noexcept
		{
			return m_Impl != nullptr;
		}

	private:
		void
		Retain() noexcept
		{
			if (m_RefCount)
			{
				++(*m_RefCount);
			}
		}

		void
		Release() noexcept
		{
			if (m_RefCount)
			{
				--(*m_RefCount);
				if (*m_RefCount == 0)
				{
					delete m_Impl;
					delete m_RefCount;
					m_Impl     = nullptr;
					m_RefCount = nullptr;
				}
			}
		}

	private:
		ImplType* m_Impl     = nullptr;
		size_t*   m_RefCount = nullptr;
	};
}
