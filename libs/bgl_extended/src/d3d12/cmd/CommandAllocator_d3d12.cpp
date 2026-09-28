#include "cmd/CommandAllocator_d3d12.h"

namespace bgl
{
	void
	CommandAllocator::ResetAllocator() noexcept
	{
		m_CommandAllocator->Reset() >> c_D3d12ErrChecker;
	}
}
