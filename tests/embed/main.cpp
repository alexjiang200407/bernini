#include <assetlib/AssetStore.h>
#include <bgl/IGraphics.h>
#include <gamelib/AssetManager.h>

#include <cstdio>
#include <filesystem>

/**
 * One translation unit outside the engine, including the public headers of the three libraries a
 * game links, run from the directory it was built into.
 *
 * It stands nothing up. What is under test is that these headers parse, that the link line
 * resolves from a build the engine did not configure, that the renderer's library loads beside the
 * executable, and that the shaders and assets it resolves from the working directory are there. A
 * device or a project on disk would only add ways to fail that say nothing about any of that.
 */
int
main()
{
	static_assert(sizeof(bgl::GraphicsOptions) > 0);
	static_assert(sizeof(assetlib::AssetStore) > 0);
	static_assert(sizeof(game::AssetManager) > 0);

	// Taking the address imports the symbol, so the loader must find the renderer before main runs;
	// a sizeof would let MSVC drop the import and the DLL with it.
	auto* volatile createGraphics = &bgl::CreateGraphics;
	if (createGraphics == nullptr)
		return 1;

	int missing = 0;
	for (const auto* staged : { "shaders", "assets" })
	{
		if (!std::filesystem::is_directory(staged))
		{
			std::fprintf(stderr, "bernini_embed: no '%s' beside the executable\n", staged);
			++missing;
		}
	}
	return missing == 0 ? 0 : 1;
}
