#include <assetlib/banim_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/bvat_io.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/Skeleton.h>

#include <catch2/catch_test_macros.hpp>

using namespace assetlib;

/*
 * A canary per cache-entry container: a fixed fixture, serialized, hashed, and pinned beside the
 * container's bake token. A change to what a writer emits -- layout, order, encoding -- moves the
 * hash; if the token did not move with it, every file on disk still carries the old token, reads
 * as current, and parses as garbage, because the chunks are schema-less. This test is where that
 * forgetting fails, in the same PR that made the change.
 *
 * On a failure, three causes, in order of likelihood:
 *   - a fixture changed: revert it. The fixtures are pinned, not maintained -- they exist to hold
 *     bytes still, and editing one is editing the measuring stick.
 *   - the output moved and the token did not: bump the container's AssetCodec<T>::c_BakeToken to
 *     a fresh random value, then re-pin both values here.
 *   - the token moved: re-pin both values here.
 *
 * The fixtures are hand-built from literals -- no bake, no file, no stamp of anything real -- so
 * the bytes are identical on every platform. That determinism is itself load-bearing: `migrate`
 * byte-compares a re-serialization against disk, so a writer whose bytes varied by machine would
 * re-report every file on every other machine. Within one struct every assigned field holds a
 * *distinct* value, so a field reorder -- which `sizeof` static_asserts cannot see -- moves the
 * hash too.
 */

namespace
{
	// Not core::hash_bytes: core/hash.h disclaims stability across changes to itself, and a pin
	// must not fail because the hash function improved -- that failure would read as "bump the
	// bake tokens", which is exactly the wrong action.
	uint64_t
	Fnv1a(std::span<const std::byte> bytes) noexcept
	{
		uint64_t hash = 0xcbf29ce484222325ull;
		for (const std::byte b : bytes)
		{
			hash ^= static_cast<uint64_t>(b);
			hash *= 0x100000001b3ull;
		}
		return hash;
	}

	struct Pin
	{
		uint64_t token;
		uint64_t hash;
	};

	SourceRef
	FixedSource()
	{
		return SourceRef{ "meshes_src/unit.glb", SourceStamp{ 64, 0x1234 }, 0x77 };
	}

	void
	CheckCanary(uint64_t token, Pin pinned, std::span<const std::byte> bytes)
	{
		const uint64_t hash = Fnv1a(bytes);
		INFO(std::format("live: token {:#018x}, output hash {:#018x}", token, hash));

		if (token != pinned.token)
			FAIL("the bake token moved -- re-pin {token, hash} in this test");

		INFO(
			"the serialized output moved but the bake token did not. If a fixture in this file "
			"changed, revert it -- the fixtures are pinned, not maintained. Otherwise the writer "
			"changed: every file on disk still carries the old token, reads as current, and "
			"parses as garbage -- bump AssetCodec<T>::c_BakeToken to a fresh random value, "
			"then re-pin both values here.");
		CHECK(hash == pinned.hash);
	}

	// Frozen: deliberately not shared with Container_test's MakeSampleMesh, which is a round-trip
	// fixture and free to grow a field -- this one may not.
	BMesh
	CanaryMesh()
	{
		BMesh mesh;

		Node root{};
		root.localTransform = { glm::vec3(1.0f, 2.0f, 3.0f),
			                    glm::quat(0.5f, 0.1f, 0.2f, 0.3f),
			                    glm::vec3(4.0f, 5.0f, 6.0f) };
		root.parent         = c_InvalidIndex;
		root.firstChild     = 0x11;
		root.nextSibling    = 0x12;
		root.mesh           = 0x13;
		root.nameOffset     = mesh.stringPool.add("root");
		mesh.nodes          = { root };
		mesh.roots          = { 0 };

		Submesh submesh{};
		submesh.layout.attributeCount = 2;
		submesh.layout.stride         = 0x30;
		submesh.layout.attributes[0]  = { static_cast<VertexSemantic>(1),
			                              static_cast<VertexFormat>(2),
			                              0x10 };
		submesh.layout.attributes[1]  = { static_cast<VertexSemantic>(3),
			                              static_cast<VertexFormat>(4),
			                              0x14 };
		submesh.vertexByteOffset      = 0x20;
		submesh.vertexCount           = 3;
		submesh.indexByteOffset       = 0x21;
		submesh.indexCount            = 6;
		submesh.indexType             = IndexType::kUint16;
		submesh.firstMeshlet          = 0x22;
		submesh.meshletCount          = 0x23;
		submesh.material              = 0x24;
		submesh.aabbMin               = glm::vec3(7.0f, 8.0f, 9.0f);
		submesh.aabbMax               = glm::vec3(10.0f, 11.0f, 12.0f);
		submesh.nameOffset            = mesh.stringPool.add("submesh");
		mesh.submeshes                = { submesh };

		mesh.meshes = { Mesh{ 0x30, 0x31, mesh.stringPool.add("mesh") } };

		Meshlet meshlet{};
		meshlet.vertexOffset   = 0x40;
		meshlet.triangleOffset = 0x41;
		meshlet.vertexCount    = 0x42;
		meshlet.triangleCount  = 0x43;
		meshlet.boundingCenter = glm::vec3(13.0f, 14.0f, 15.0f);
		meshlet.boundingRadius = 16.0f;
		mesh.meshlets          = { meshlet };
		mesh.meshletVertices   = { 5, 6, 7 };
		mesh.meshletTriangles  = { 8, 9, 10 };

		mesh.vertexData.resize(3 * 48);
		for (size_t i = 0; i < mesh.vertexData.size(); ++i)
			mesh.vertexData[i] = static_cast<std::byte>(i);
		mesh.indexData = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 },
			               std::byte{ 4 }, std::byte{ 5 }, std::byte{ 6 } };

		mesh.materials = { "Materials/unit.bmaterial" };
		mesh.skeleton  = "Skeletons/unit.bskel";
		mesh.source    = FixedSource();
		return mesh;
	}

	Skeleton
	CanarySkeleton()
	{
		Skeleton skeleton;

		Bone bone{};
		bone.bindPose    = { glm::vec3(1.0f, 2.0f, 3.0f),
			                 glm::quat(0.5f, 0.1f, 0.2f, 0.3f),
			                 glm::vec3(4.0f, 5.0f, 6.0f) };
		bone.inverseBind = glm::mat4(
			1.0f,
			2.0f,
			3.0f,
			4.0f,
			5.0f,
			6.0f,
			7.0f,
			8.0f,
			9.0f,
			10.0f,
			11.0f,
			12.0f,
			13.0f,
			14.0f,
			15.0f,
			16.0f);
		bone.parent     = c_InvalidIndex;
		bone.nameOffset = skeleton.stringPool.add("hip");
		skeleton.bones  = { bone };
		skeleton.source = FixedSource();
		return skeleton;
	}

	AnimationSet
	CanaryAnimations()
	{
		AnimationSet animations;
		animations.skeleton          = "Skeletons/unit.bskel";
		animations.skeletonSignature = 0xfeed;
		animations.boneCount         = 1;

		AnimationClip clip{};
		clip.nameOffset      = animations.stringPool.add("walk");
		clip.firstSample     = 0;
		clip.frameCount      = 2;
		clip.sampleRate      = 24.0f;
		clip.duration        = 0.5f;
		clip.rootMotion      = glm::vec3(17.0f, 18.0f, 19.0f);
		clip.locomotionSpeed = 2.5f;
		clip.loop            = 1;
		animations.clips     = { clip };

		animations.samples = { Transform{ glm::vec3(1.0f, 2.0f, 3.0f),
			                              glm::quat(0.5f, 0.1f, 0.2f, 0.3f),
			                              glm::vec3(4.0f, 5.0f, 6.0f) },
			                   Transform{ glm::vec3(7.0f, 8.0f, 9.0f),
			                              glm::quat(0.6f, 0.4f, 0.3f, 0.2f),
			                              glm::vec3(10.0f, 11.0f, 12.0f) } };

		PosedBox box{};
		box.sourceSignature   = 0xbeef;
		box.min               = glm::vec3(20.0f, 21.0f, 22.0f);
		box.max               = glm::vec3(23.0f, 24.0f, 25.0f);
		box.meshIndex         = 0x70;
		animations.posedBoxes = { box };

		animations.source = FixedSource();
		return animations;
	}

	BVat
	CanaryVat()
	{
		BVat vat;
		vat.boundsMin = glm::vec3(-1.0f, -2.0f, -3.0f);
		vat.boundsMax = glm::vec3(4.0f, 5.0f, 6.0f);
		vat.width     = 3;
		vat.height    = 5;  // two clips: (1 + 1) + (2 + 1) padded rows
		vat.boneCount = 2;

		// Two clips, so the second's row and palette bases are non-zero and distinct -- the first
		// clip's are forced to zero by validateVat, which cannot witness a reorder.
		VatClip walk{};
		walk.nameOffset   = vat.stringPool.add("walk");
		walk.firstRow     = 0;
		walk.frameCount   = 1;
		walk.firstPalette = 0;
		walk.sampleRate   = 24.0f;
		walk.duration     = 0.5f;
		walk.loop         = 1;

		VatClip idle{};
		idle.nameOffset   = vat.stringPool.add("idle");
		idle.firstRow     = 2;
		idle.frameCount   = 2;
		idle.firstPalette = 2;
		idle.sampleRate   = 30.0f;
		idle.duration     = 0.75f;
		idle.loop         = 0;

		vat.clips    = { walk, idle };
		vat.columns  = { VatColumns{ 0, 1 }, VatColumns{ 1, 2 } };
		vat.palettes = { glm::mat4(2.0f), glm::mat4(3.0f), glm::mat4(4.0f),
			             glm::mat4(5.0f), glm::mat4(6.0f), glm::mat4(7.0f) };

		vat.mesh              = "Meshes/unit.bmesh";
		vat.skeleton          = "Skeletons/unit.bskel";
		vat.animations        = "Animations/unit.banim";
		vat.skeletonSignature = 0xfeed;
		vat.meshStamp         = SourceStamp{ 1, 2 };
		vat.skeletonStamp     = SourceStamp{ 3, 4 };
		vat.animationsStamp   = SourceStamp{ 5, 6 };

		// Opaque payloads to the writer; literal bytes keep the hash off libktx entirely.
		vat.positionsKtx2.resize(16);
		vat.normalsKtx2.resize(16);
		for (size_t i = 0; i < 16; ++i)
		{
			vat.positionsKtx2[i] = static_cast<std::byte>(0xA0 + i);
			vat.normalsKtx2[i]   = static_cast<std::byte>(0xC0 + i);
		}
		return vat;
	}

	BSky
	CanarySky()
	{
		BSky sky;
		sky.name       = "canary";
		sky.sky.source = "textures_src/canary.ktx2";
		sky.sky.baked  = "Textures/canary_sky_0123456789abcdef.ktx2";
		sky.sky.stamp  = SourceStamp{ 7, 8 };
		return sky;
	}

	BEnvLighting
	CanaryLighting()
	{
		BEnvLighting lighting;
		lighting.name              = "canary";
		lighting.prefilter.source  = "textures_src/canary_pre.ktx2";
		lighting.prefilter.baked   = "Textures/canary_prefilter_0123456789abcdef.ktx2";
		lighting.prefilter.stamp   = SourceStamp{ 9, 10 };
		lighting.irradiance.source = "textures_src/canary_irr.ktx2";
		lighting.irradiance.baked  = "Textures/canary_irradiance_0123456789abcdef.ktx2";
		lighting.irradiance.stamp  = SourceStamp{ 11, 12 };
		lighting.exposure          = 1.5f;
		return lighting;
	}
}

TEST_CASE("a writer's output cannot change without its bake token", "[canary][io]")
{
	SECTION(".bmesh")
	{
		CheckCanary(
			AssetCodec<BMesh>::c_BakeToken,
			Pin{ .token = 0x6f1d3a58c2e94b07ull, .hash = 0x08f0b7bf5f59fb2cull },
			serialize(CanaryMesh()));
	}

	SECTION(".bskel")
	{
		CheckCanary(
			AssetCodec<Skeleton>::c_BakeToken,
			Pin{ .token = 0x9be47d02a15c68f3ull, .hash = 0x3dd4d201c9b7ea0bull },
			serializeSkeleton(CanarySkeleton()));
	}

	SECTION(".banim")
	{
		CheckCanary(
			AssetCodec<AnimationSet>::c_BakeToken,
			Pin{ .token = 0x41f8b6d95e07c2aaull, .hash = 0xffbf10e60751b032ull },
			serializeAnimations(CanaryAnimations()));
	}

	SECTION(".bvat")
	{
		CheckCanary(
			AssetCodec<BVat>::c_BakeToken,
			Pin{ .token = 0x25b90ce8f7143ad9ull, .hash = 0x6e93b48621d0aca0ull },
			serializeVat(CanaryVat()));
	}

	SECTION(".bsky")
	{
		CheckCanary(
			AssetCodec<BSky>::c_BakeToken,
			Pin{ .token = 0x7c25e8b1904dfa36ull, .hash = 0xc4ad4035fc805a1full },
			serializeSky(CanarySky()));
	}

	SECTION(".benvl")
	{
		CheckCanary(
			AssetCodec<BEnvLighting>::c_BakeToken,
			Pin{ .token = 0xd48f19c7a35b062eull, .hash = 0x20569dd2f51e76d8ull },
			serializeEnvLighting(CanaryLighting()));
	}
}
