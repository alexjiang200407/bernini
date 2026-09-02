#pragma once

namespace assetlib
{
	// A source's meshes are the largest thing in this library and one cook holds a whole parse, so
	// what bounds a cook that fans out is memory rather than cores.
	constexpr uint32_t c_MaxCookThreads = 4;

	// What the profiler shows every cook worker as, whichever stage spawned it.
	constexpr const char* c_CookThreadName = "assetlib cook";
}
