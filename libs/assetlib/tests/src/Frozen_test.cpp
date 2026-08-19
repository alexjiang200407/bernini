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

/*
 * The live change the frozen set exists for, run against the frozen bytes: the engine's struct
 * gains a field and loses one, and the file written before either happened still reads. Done with
 * a "next" schema over the real chunk::Reader rather than by changing the engine's structs, so the
 * suite exercises the path a real change takes without one having to happen.
 */
namespace
{
	/** Submesh after a change: nameOffset gone, lodBias added at the end. */
	struct SubmeshNext
	{
		VertexLayout layout;
		uint32_t     vertexByteOffset;
		uint32_t     vertexCount;
		uint32_t     indexByteOffset;
		uint32_t     indexCount;
		IndexType    indexType;
		uint32_t     firstMeshlet;
		uint32_t     meshletCount;
		uint32_t     material;
		glm::vec3    aabbMin;
		glm::vec3    aabbMax;
		float        lodBias;
	};

	static_assert(sizeof(SubmeshNext) == sizeof(Submesh));

	const schema::Schema&
	NextMeshSchema()
	{
		static const schema::Schema c_Schema =
			AssetSchemaBuilder()
				.AddTransform()
				.AddNode()
				.AddMesh()
				.AddVertexLayout()
				.AddLayout<SubmeshNext>(
					"Submesh",
					[](auto& l) {
						l.AddField("layout", &SubmeshNext::layout)
							.AddField("vertexByteOffset", &SubmeshNext::vertexByteOffset)
							.AddField("vertexCount", &SubmeshNext::vertexCount)
							.AddField("indexByteOffset", &SubmeshNext::indexByteOffset)
							.AddField("indexCount", &SubmeshNext::indexCount)
							.AddField("indexType", &SubmeshNext::indexType)
							.AddField("firstMeshlet", &SubmeshNext::firstMeshlet)
							.AddField("meshletCount", &SubmeshNext::meshletCount)
							.AddField("material", &SubmeshNext::material)
							.AddField("aabbMin", &SubmeshNext::aabbMin)
							.AddField("aabbMax", &SubmeshNext::aabbMax)
							.AddField("lodBias", &SubmeshNext::lodBias, 1.0f);
					})
				.AddMeshlet()
				.Finish();
		return c_Schema;
	}

	/** bmaterial_io's RouteRecord, as it is: an unchanged nested layout is a copy. */
	struct RouteRecordNext
	{
		uint32_t textureOffset;
		uint16_t channel;
		uint16_t pad;
	};

	/** PbrRecord after a change: roughnessFactor gone, sheen added; the arrays untouched. */
	struct PbrRecordNext
	{
		glm::vec4                                        baseColorFactor;
		float                                            metallicFactor;
		float                                            sheen;
		uint32_t                                         alphaMode;
		float                                            alphaCutoff;
		float                                            transmissionFactor;
		uint32_t                                         baseColorTextureOffset;
		uint32_t                                         normalTextureOffset;
		uint32_t                                         ormTextureOffset;
		std::array<RouteRecordNext, c_LooseChannelCount> routes;
		std::array<SourceStamp, c_LooseChannelCount>     routeStamps;
	};

	const schema::Schema&
	NextMaterialSchema()
	{
		static const schema::Schema c_Schema =
			AssetSchemaBuilder()
				.AddSourceStamp()
				.AddLayout<RouteRecordNext>(
					"RouteRecord",
					[](auto& l) {
						l.AddField("textureOffset", &RouteRecordNext::textureOffset)
							.AddField("channel", &RouteRecordNext::channel)
							.AddField("pad", &RouteRecordNext::pad);
					})
				.AddLayout<PbrRecordNext>(
					"PbrRecord",
					[](auto& l) {
						l.AddField("baseColorFactor", &PbrRecordNext::baseColorFactor)
							.AddField("metallicFactor", &PbrRecordNext::metallicFactor)
							.AddField("sheen", &PbrRecordNext::sheen, 0.25f)
							.AddField("alphaMode", &PbrRecordNext::alphaMode)
							.AddField("alphaCutoff", &PbrRecordNext::alphaCutoff)
							.AddField("transmissionFactor", &PbrRecordNext::transmissionFactor)
							.AddField(
								"baseColorTextureOffset",
								&PbrRecordNext::baseColorTextureOffset)
							.AddField("normalTextureOffset", &PbrRecordNext::normalTextureOffset)
							.AddField("ormTextureOffset", &PbrRecordNext::ormTextureOffset)
							.AddField("routes", &PbrRecordNext::routes)
							.AddField("routeStamps", &PbrRecordNext::routeStamps);
					})
				.Finish();
		return c_Schema;
	}
}

TEST_CASE(
	"the frozen .bmesh reads through a struct that gained a field and lost one",
	"[frozen][bmesh][schema]")
{
	const auto bytes = core::file::read_file_bytes(std::string("assets/Frozen/apples_v4.bmesh"));
	const auto today = load(std::filesystem::path("assets/Frozen/apples_v4.bmesh"));

	const chunk::Reader reader(bytes, magic::c_BMesh, 4, "bmesh", NextMeshSchema());
	const auto          next = reader.Read<SubmeshNext>(4u);  // bmesh_io's ChunkId::kSubmeshes
	REQUIRE(next.size() == today.submeshes.size());
	for (size_t i = 0; i < next.size(); ++i)
	{
		CHECK(next[i].vertexCount == today.submeshes[i].vertexCount);
		CHECK(next[i].indexCount == today.submeshes[i].indexCount);
		CHECK(next[i].indexType == today.submeshes[i].indexType);
		CHECK(next[i].material == today.submeshes[i].material);
		CHECK(next[i].aabbMin == today.submeshes[i].aabbMin);
		CHECK(next[i].layout.stride == today.submeshes[i].layout.stride);
		CHECK(next[i].layout.attributeCount == today.submeshes[i].layout.attributeCount);
		CHECK(next[i].lodBias == 1.0f);  // never stored: the default
	}

	// The chunks whose layouts did not change are copies, through the same reader.
	CHECK(reader.Read<Node>(1u).size() == today.nodes.size());
	CHECK(reader.Read<Meshlet>(5u).size() == today.meshlets.size());
}

TEST_CASE(
	"the frozen .bmaterial reads through a record that gained a field and lost one",
	"[frozen][bmaterial][schema]")
{
	const auto bytes =
		core::file::read_file_bytes(std::string("assets/Frozen/apple_v11.bmaterial"));
	const auto today = loadMaterial(std::filesystem::path("assets/Frozen/apple_v11.bmaterial"));

	const chunk::Reader reader(bytes, magic::c_BMaterial, 11, "bmaterial", NextMaterialSchema());
	const auto          pbr = reader.Read<PbrRecordNext>(3u);  // bmaterial_io's ChunkId::kPbr
	REQUIRE(pbr.size() == 1);
	CHECK(pbr[0].baseColorFactor == today.pbr.baseColorFactor);
	CHECK(pbr[0].metallicFactor == today.pbr.metallicFactor);
	CHECK(pbr[0].sheen == 0.25f);  // never stored: the default
	CHECK(pbr[0].alphaMode == static_cast<uint32_t>(today.pbr.alphaMode));
	CHECK(pbr[0].transmissionFactor == today.pbr.transmissionFactor);
	for (size_t i = 0; i < c_LooseChannelCount; ++i)
	{
		CHECK(pbr[0].routes[i].channel == today.pbr.routes[i].channel);
		CHECK(pbr[0].routeStamps[i] == today.pbr.routeStamps[i]);
	}
	// A route's texture path is a pool offset; the pool chunk is a copy of bytes either way.
	CHECK(reader.Read<char>(2u).size() > 0);
}
