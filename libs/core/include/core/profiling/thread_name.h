#pragma once

namespace core::profiling
{
	/**
	 * Names the calling thread for the profiler, so its track reads as a name and not an id.
	 *
	 * The one place in the tree that knows profiling can be compiled out. Tracy's zone macros
	 * degrade to nothing on their own, but `tracy::SetThreadName` is an ordinary function whose
	 * definition lives in the client -- so without this a `BERNINI_PROFILING=OFF` build fails to
	 * link, once per thread anybody ever named.
	 *
	 * **The first name a thread is given is the one it keeps**, so a pooled worker may call this at
	 * the top of every task without announcing itself on each one. That is the common case: a task
	 * knows what pool it is on, and a thread body often does not exist to put the call in.
	 */
	void
	name_this_thread(const char* name) noexcept;
}
