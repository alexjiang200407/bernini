#pragma once

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class ICommandAllocator : public core::Ref
	{
	public:
		ICommandAllocator() noexcept = default;

		ICommandAllocator(ICommandAllocator&&)      = delete;
		ICommandAllocator(const ICommandAllocator&) = delete;

		ICommandAllocator&
		operator=(ICommandAllocator&&) = delete;

		ICommandAllocator&
		operator=(const ICommandAllocator&) = delete;

		virtual void
		ResetAllocator() noexcept = 0;
	};

	using CommandAllocatorHandle = core::SharedRef<ICommandAllocator>;
}
