#include "cmd/CommandAllocator_d3d12.h"

namespace bgl
{
	void
	CommandAllocatorImpl::Reset()
	{
		gassert(m_CommandAllocator != nullptr, "Command allocator cannot be null");
		m_CommandAllocator->Reset() >> d3d12ErrChecker;
	}

	void
	CommandAllocator::Reset()
	{
		gassert(IsInitialized(), "Command allocator is not initialized");
		GetImpl()->Reset();
	}
}
