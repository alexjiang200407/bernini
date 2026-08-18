#include <assetlib/banim_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

using namespace assetlib;

/*
 * assets/Frozen holds one file per container, written at the first schema that container ever
 * carried, and never rewritten. Every schema change after that is measured against them here: a
 * layout edit that leaves these unreadable is a layout edit that leaves every project unreadable,
 * and this is where it fails first.
 */

TEST_CASE("the first self-describing .bmesh still loads", "[frozen][bmesh]")
{
	const auto mesh = load(std::filesystem::path("assets/Frozen/apples_v4.bmesh"));
	CHECK(mesh.nodes.size() == 4);
	CHECK(mesh.meshes.size() == 2);
	CHECK(mesh.submeshes.size() == 2);
	CHECK(mesh.materials.size() == 2);
	CHECK_FALSE(mesh.meshlets.empty());
	CHECK_FALSE(mesh.vertexData.empty());
	CHECK(mesh.skeleton.empty());
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
	CHECK(
		loadAnimationSkeletonPath(std::filesystem::path("assets/Frozen/rig_v2.banim")) ==
		"Skeletons/rig.bskel");
}

TEST_CASE("the first self-describing .bmaterial still loads", "[frozen][bmaterial]")
{
	const auto material = loadMaterial(std::filesystem::path("assets/Frozen/apple_v11.bmaterial"));
	CHECK(material.shadingModel == ShadingModel::kPbr);
	CHECK_FALSE(material.name.empty());
	CHECK_FALSE(material.pbr.baseColorTexture.empty());
	CHECK(material.pbr.alphaMode == AlphaMode::kOpaque);
	CHECK(material.pbr.baseColorFactor == glm::vec4(1.0f));
}
