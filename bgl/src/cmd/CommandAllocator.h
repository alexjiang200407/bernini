#pragma once
#include <core/pimpl/RefCountPImpl.h>

namespace bgl
{
	class CommandAllocatorImpl;
	class CommandAllocator : public core::RefCountPImpl<CommandAllocatorImpl>
	{
	public:
		CommandAllocator() = default;

		[[noreturn]]
		void
		Reset();

		friend class CommandListImpl;
		friend class DeviceImpl;
	};
}
