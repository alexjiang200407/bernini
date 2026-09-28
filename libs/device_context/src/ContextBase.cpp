#include "ContextBase.h"
#include <atomic>
#include <core/err/util.h>

namespace gpu
{
	namespace
	{
		std::atomic<bool> g_Live = false;
	}

	ContextBase::ProcessSlot::ProcessSlot()
	{
		if (g_Live.exchange(true))
		{
			core::throw_runtime_error(
				"a device context is already live in this process: one device per process, handed "
				"to every owner");
		}
	}

	ContextBase::ProcessSlot::~ProcessSlot() noexcept { g_Live.store(false); }
}
