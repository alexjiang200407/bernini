#include "AutoreleaseNet_metal.h"
#include "metal_cpp.h"
#include <cstdint>

namespace bgl
{
	namespace
	{
		struct ThreadNet
		{
			NS::AutoreleasePool* pool   = nullptr;
			uint32_t             shares = 0;
		};

		thread_local ThreadNet g_Net;
	}

	AutoreleaseNet::AutoreleaseNet() noexcept
	{
		if (g_Net.shares++ == 0)
			g_Net.pool = NS::AutoreleasePool::alloc()->init();
	}

	AutoreleaseNet::~AutoreleaseNet() noexcept
	{
		gassert(g_Net.shares > 0, "An autorelease net share was released on another thread");
		if (--g_Net.shares == 0)
		{
			g_Net.pool->release();
			g_Net.pool = nullptr;
		}
	}
}
