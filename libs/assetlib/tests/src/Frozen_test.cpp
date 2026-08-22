#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>

#include "AssetSchemaBuilder.h"
#include "MountAt.h"
#include "chunk_io.h"
#include "mounted_io.h"

#include <core/file/file.h>

using namespace assetlib;

/*
 * assets/Frozen holds one file per container, written at the first schema that container ever
 * carried, and never rewritten. Every schema change after that is measured against them here: a
 * layout edit that leaves these unreadable is a layout edit that leaves every project unreadable,
 * and this is where it fails first.
 */

TEST_CASE("the first self-describing env containers still load", "[frozen][benv]")
{
	const auto sky = loadSky(std::filesystem::path("assets/Frozen/forest_v3.bsky"));
	CHECK(sky.name == "forest");
	CHECK_FALSE(sky.sky.baked.empty());

	const auto lighting = loadEnvLighting(std::filesystem::path("assets/Frozen/forest_v3.benvl"));
	CHECK(lighting.name == "forest");
	CHECK_FALSE(lighting.prefilter.baked.empty());
	CHECK_FALSE(lighting.irradiance.baked.empty());
	CHECK(lighting.exposure > 0.0f);

	const auto env = loadEnv(std::filesystem::path("assets/Frozen/forest_v3.benv"));
	CHECK(env.name == "forest");
	CHECK(env.sky == "Sky/forest.bsky");
	CHECK(env.lighting == "EnvLighting/forest.benvl");

	CHECK(loadSky(MountAt("assets/Frozen"), "forest_v3.bsky").name == "forest");
	CHECK(
		loadEnvLighting(MountAt("assets/Frozen"), "forest_v3.benvl").exposure == lighting.exposure);
	CHECK(loadEnv(MountAt("assets/Frozen"), "forest_v3.benv").sky == env.sky);
}
