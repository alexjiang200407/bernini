#include "cmd/CommandAllocator_d3d12.h"

namespace bgl
{
	void
	CommandAllocator::ResetAllocator()
	{
		gassert(m_CommandAllocator != nullptr, "Command allocator cannot be null");
		m_CommandAllocator->Reset() >> d3d12ErrChecker;
	}
}
