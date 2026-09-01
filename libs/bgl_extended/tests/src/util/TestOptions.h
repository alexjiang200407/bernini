#pragma once

namespace bgl::test
{
	/**
	 * The persistent shader cache directory every test's `CreateGraphics` should use, relative to
	 * the binary's output directory.
	 *
	 * One directory is shared by the whole suite: compiling shaders is nearly all of what device
	 * creation costs, so the first device pays it and every later one loads instead. Staleness
	 * cannot cause a false pass -- the cache salt folds the content of every shader source, so an
	 * edited shader misses every key rather than being misread, and an unreadable entry is a miss
	 * too. The directory is disposable; deleting it costs one slow run.
	 *
	 * Safe to use with GPU-based validation: the backend drops the driver PSO layer by itself when
	 * validation is on, and keeps the program cache, which validation does not affect.
	 */
	[[nodiscard]] std::string
	ShaderCacheDir();
}
