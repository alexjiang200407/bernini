#pragma once

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class ICommandAllocator : public core::Ref
	{
	public:
		virtual void
		ResetAllocator() = 0;
	};

	using CommandAllocatorHandle = core::SharedRef<ICommandAllocator>;
}
