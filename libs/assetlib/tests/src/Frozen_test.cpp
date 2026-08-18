#include <assetlib/banim_io.h>
#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include "MountAt.h"
#include "mounted_io.h"

using namespace assetlib;

/*
 * assets/Frozen holds one file per container, written at the first schema that container ever
 * carried, and never rewritten. Every schema change after that is measured against them here: a
 * layout edit that leaves these unreadable is a layout edit that leaves every project unreadable,
 * and this is where it fails first.
 */

TEST_CASE("the first self-describing .bmesh still loads, through every path", "[frozen][bmesh]")
{
	// Whole file from disk, whole file through a mount, and the ranged reference read -- the three
	// ways a container is opened, and each converts through the same schema.
	const auto mesh = load(std::filesystem::path("assets/Frozen/apples_v4.bmesh"));
	CHECK(mesh.nodes.size() == 4);
	CHECK(mesh.meshes.size() == 2);
	CHECK(mesh.submeshes.size() == 2);
	CHECK(mesh.materials.size() == 2);
	CHECK_FALSE(mesh.meshlets.empty());
	CHECK_FALSE(mesh.vertexData.empty());
	CHECK(mesh.skeleton.empty());

	const auto mounted = load(MountAt("assets/Frozen"), "apples_v4.bmesh");
	CHECK(mounted.submeshes.size() == 2);
	CHECK(mounted.vertexData == mesh.vertexData);

	const auto refs = loadMeshRefs(std::filesystem::path("assets/Frozen/apples_v4.bmesh"));
	CHECK(refs.materials == mesh.materials);
	CHECK(refs.skeleton.empty());
	CHECK(loadMeshRefs(MountAt("assets/Frozen"), "apples_v4.bmesh").materials == mesh.materials);
}

TEST_CASE(
	"the first self-describing .bskel and .banim still load, and still match",
	"[frozen][bskel][banim]")
{
	const auto skeleton   = loadSkeleton(std::filesystem::path("assets/Frozen/rig_v2.bskel"));
	const auto animations = loadAnimations(std::filesystem::path("assets/Frozen/rig_v2.banim"));
	CHECK(skeleton.bones.size() == 2);
	CHECK(skeleton.stringPool.at(skeleton.bones[1].nameOffset) == "child");
	CHECK(animations.clips.size() == 2);
	CHECK(animations.skeleton == "Skeletons/rig.bskel");
	CHECK(animations.boneCount == 2);
	CHECK(animationsMatchSkeleton(animations, skeleton));

	// The ranged read and the mounted overloads open the same files through their other doors.
	CHECK(
		loadAnimationSkeletonPath(std::filesystem::path("assets/Frozen/rig_v2.banim")) ==
		"Skeletons/rig.bskel");
	CHECK(
		loadAnimationSkeletonPath(MountAt("assets/Frozen"), "rig_v2.banim") ==
		"Skeletons/rig.bskel");
	CHECK(loadSkeleton(MountAt("assets/Frozen"), "rig_v2.bskel").bones.size() == 2);
	CHECK(loadAnimations(MountAt("assets/Frozen"), "rig_v2.banim").clips.size() == 2);
}

TEST_CASE("the first self-describing .bmaterial still loads", "[frozen][bmaterial]")
{
	const auto material = loadMaterial(std::filesystem::path("assets/Frozen/apple_v11.bmaterial"));
	CHECK(material.shadingModel == ShadingModel::kPbr);
	CHECK_FALSE(material.name.empty());
	CHECK_FALSE(material.pbr.baseColorTexture.empty());
	CHECK(material.pbr.alphaMode == AlphaMode::kOpaque);
	CHECK(material.pbr.baseColorFactor == glm::vec4(1.0f));
	CHECK(loadMaterial(MountAt("assets/Frozen"), "apple_v11.bmaterial").name == material.name);
}

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
