#pragma once

namespace core
{
	/**
	 * Runs `body(i)` for every `i` below `count`, across `threads` threads, and returns once every
	 * one of them has run. `threads` of 0 means one per hardware thread; either way no more than
	 * `count` are started, and a count of 1 runs on the calling thread.
	 *
	 * Indices are claimed from a shared counter rather than partitioned up front, so items of
	 * uneven cost spread themselves. Nothing orders them and nothing synchronizes what a body
	 * touches: an item that depends on another's output belongs in a later call.
	 *
	 * `threadName` is what the profiler shows for the workers (see
	 * core::profiling::name_this_thread).
	 *
	 * @throws The first exception a body threw, rethrown once every thread has finished -- so a
	 *         throwing body neither leaves a worker running past the return nor reaches
	 *         std::thread's terminate.
	 */
	void
	parallel_for(
		size_t                             count,
		uint32_t                           threads,
		const char*                        threadName,
		const std::function<void(size_t)>& body);
}
