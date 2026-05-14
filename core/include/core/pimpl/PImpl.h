#pragma once

namespace core
{
	template <typename ImplType>
	class PImpl
	{
	protected:
		PImpl() noexcept = default;
		PImpl(ImplType* impl) noexcept : m_Impl(impl) {}

		template <typename... Args>
		PImpl(Args... args) noexcept : m_Impl(new ImplType(std::forward<Args>(args)...))
		{}

		~PImpl()
		{
			if (m_Impl)
				delete m_Impl;
		}

		PImpl(const PImpl&) = delete;

		PImpl&
		operator=(const PImpl&) = delete;

		PImpl(PImpl&& other) noexcept : m_Impl(other.m_Impl) { other.m_Impl = nullptr; }

		PImpl&
		operator=(PImpl&& other) noexcept
		{
			if (this != &other)
			{
				delete m_Impl;
				m_Impl       = other.m_Impl;
				other.m_Impl = nullptr;
			}
			return *this;
		}

		bool
		IsInitialized() const noexcept
		{
			return m_Impl != nullptr;
		}

		template <typename... Args>
		void
		EmplaceImpl(Args... args)
		{
			if (m_Impl)
				delete m_Impl;
			m_Impl = new ImplType(std::forward<Args>(args)...);
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

	private:
		ImplType* m_Impl = nullptr;
	};
}
