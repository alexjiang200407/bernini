#pragma once

namespace rhi
{
	class IResource
	{
	protected:
		IResource()          = default;
		virtual ~IResource() = default;

	public:
		virtual unsigned long
		AddRef() = 0;

		virtual unsigned long
		Release() = 0;

		virtual unsigned long
		GetRefCount() = 0;

		template <typename T>
			requires std::derived_from<T, IResource>
		T*
		As()
		{
			return dynamic_cast<T*>(this);
		}

		// Non-copyable and non-movable
		IResource(const IResource&)  = delete;
		IResource(const IResource&&) = delete;
		IResource&
		operator=(const IResource&) = delete;
		IResource&
		operator=(const IResource&&) = delete;
	};
}
