
namespace rhi::detail
{
	template <class T>
		requires std::derived_from<T, IResource>
	class RefCounter : public T
	{
	private:
		std::atomic<unsigned long> refCount = 1;

	public:
		virtual unsigned long
		AddRef() override
		{
			return ++refCount;
		}

		virtual unsigned long
		Release() override
		{
			unsigned long result = --refCount;
			if (result == 0)
			{
				delete this;
			}
			return result;
		}

		virtual unsigned long
		GetRefCount() override
		{
			return refCount.load();
		}
	};
}
