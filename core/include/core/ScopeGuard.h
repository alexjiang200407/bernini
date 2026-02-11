namespace core
{
	template <typename F>
	class ScopeGuard
	{
	public:
		explicit ScopeGuard(F&& f) : m_func(std::move(f)), m_active(true) {}

		ScopeGuard(const ScopeGuard&) = delete;
		ScopeGuard&
		operator=(const ScopeGuard&) = delete;

		ScopeGuard(ScopeGuard&& other) noexcept :
			m_func(std::move(other.m_func)), m_active(other.m_active)
		{
			other.m_active = false;
		}

		~ScopeGuard() noexcept(noexcept(m_func()))
		{
			if (m_active)
			{
				m_func();
			}
		}

		void
		Dismiss() noexcept
		{
			m_active = false;
		}

	private:
		F    m_func;
		bool m_active;
	};

	template <typename F>
	[[nodiscard]] auto
	make_scope_guard(F&& f)
	{
		return ScopeGuard<std::decay_t<F>>(std::forward<F>(f));
	}
}
