#pragma once

namespace assetlib
{
	// A source's meshes are the largest thing in this library and one cook holds a whole parse, so
	// what bounds a cook that fans out is memory rather than cores.
	constexpr uint32_t c_MaxCookThreads = 4;

	/**
	 * Runs `body(i)` for every `i` below `count`, across `threads` threads, and returns once every
	 * one of them has run. `threads` of 0 means one per hardware thread.
	 *
	 * Indices are claimed from a shared counter rather than partitioned up front, so items of
	 * uneven cost spread themselves. Nothing orders them and nothing synchronizes what a body
	 * touches: an item that depends on another's output belongs in a later call.
	 *
	 * @throws The first exception a body threw, rethrown once every thread has finished -- so a
	 *         throwing body neither leaves a worker running past the return nor reaches
	 *         std::thread's terminate.
	 */
	void
	parallelFor(size_t count, uint32_t threads, const std::function<void(size_t)>& body);
}
