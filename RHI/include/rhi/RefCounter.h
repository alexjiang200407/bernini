
namespace rhi::detail
{
	template <class T>
		requires std::derived_from<T, IResource>
	class RefCounter : public T
	{
	private:
		std::atomic<unsigned long> m_refCount = 1;

	public:
		virtual unsigned long
		AddRef() override
		{
			return ++m_refCount;
		}

		virtual unsigned long
		Release() override
		{
			unsigned long result = --m_refCount;
			if (result == 0)
			{
				delete this;
			}
			return result;
		}

		virtual unsigned long
		GetRefCount() override
		{
			return m_refCount.load();
		}
	};
}
