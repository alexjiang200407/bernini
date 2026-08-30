#include "parallel_for.h"
#include <core/profiling/thread_name.h>

namespace assetlib
{
	void
	parallelFor(size_t count, uint32_t threads, const std::function<void(size_t)>& body)
	{
		if (count == 0)
			return;

		const uint32_t wanted =
			threads != 0 ? threads : std::max(1u, std::thread::hardware_concurrency());

		// The min is at most `wanted`, so it always fits -- but it is computed in size_t to compare
		// against `count`, and MSVC will not narrow that back for free.
		const auto threadCount = static_cast<uint32_t>(std::min<size_t>(count, wanted));

		auto next    = std::atomic<size_t>(0);
		auto guard   = std::mutex();
		auto failure = std::exception_ptr();

		auto worker = [&]() {
			// Every cook that fans out runs through here, so one name covers Reimport's stages and
			// Migrate's resave walk both -- a capture of a rebuild is otherwise a row of thread ids.
			core::profiling::name_this_thread("assetlib cook");

			for (;;)
			{
				const size_t index = next.fetch_add(1);
				if (index >= count)
					return;

				try
				{
					body(index);
				}
				catch (...)
				{
					const auto held = std::lock_guard(guard);
					if (!failure)
						failure = std::current_exception();
				}
			}
		};

		if (threadCount == 1)
		{
			worker();
		}
		else
		{
			auto pool = std::vector<std::thread>();
			pool.reserve(threadCount);
			for (uint32_t i = 0; i < threadCount; ++i) pool.emplace_back(worker);
			for (std::thread& thread : pool) thread.join();
		}

		if (failure)
			std::rethrow_exception(failure);
	}
}
