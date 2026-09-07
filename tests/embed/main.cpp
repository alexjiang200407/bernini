#include <assetlib/AssetStore.h>
#include <bgl/IGraphics.h>
#include <gamelib/AssetManager.h>

/**
 * One translation unit outside the engine, including the public headers of the three libraries a
 * game links.
 *
 * It stands nothing up. What is under test is that these headers parse and that the link line
 * resolves from a build the engine did not configure; a device or a project on disk would only add
 * ways to fail that say nothing about either.
 */
int
main()
{
	static_assert(sizeof(bgl::GraphicsOptions) > 0);
	static_assert(sizeof(assetlib::AssetStore) > 0);
	static_assert(sizeof(game::AssetManager) > 0);
	return 0;
}
