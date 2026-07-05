#pragma once

namespace core
{
	class Ref
	{
	protected:
		Ref()                   = default;
		virtual ~Ref() noexcept = default;

	public:
		virtual unsigned long
		AddRef() = 0;

		virtual unsigned long
		Release() = 0;

		virtual unsigned long
		GetRefCount() = 0;

		template <typename T>
		T*
		As()
		{
			return dynamic_cast<T*>(this);
		}

		Ref(const Ref&)  = delete;
		Ref(const Ref&&) = delete;

		Ref&
		operator=(const Ref&) = delete;

		Ref&
		operator=(const Ref&&) = delete;
	};
}
