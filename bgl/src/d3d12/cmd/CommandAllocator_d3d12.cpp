#include "cmd/CommandAllocator_d3d12.h"

namespace bgl
{
	void
	CommandAllocator::ResetAllocator()
	{
		m_CommandAllocator->Reset() >> d3d12ErrChecker;
	}
}
