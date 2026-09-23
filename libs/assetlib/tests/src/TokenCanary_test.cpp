#include <assetlib/codecs.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/SourceRef.h>
#include <assetlib_structs/SourceStamp.h>
#include <assetlib_structs/VertexLayout.h>

#include <assetlib_structs/VkFormat.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/containers/fixed_buffer.h>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "bmesh_texture.h"
#include "texture_encoding.h"

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
 * One value is not a literal and cannot be: the `.bmesh` writer computes BMesh::geometrySignature
 * from the bytes it is emitting, so that a file cannot carry a hash disagreeing with its own
 * geometry. That makes this pin depend on core::hash_bytes, which core/hash.h disclaims stability
 * for -- so a fourth cause belongs on the list above: core::hash changed, in which case re-pin and
 * expect every .bmesh to re-cook, since the signatures stored in them all moved with it.
 *
 * The fixtures are otherwise hand-built from literals -- no bake, no file, no stamp of anything
 * real -- so the bytes are identical on every platform. That determinism is itself load-bearing: `migrate`
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
		submesh.firstMeshletGroup     = 0x25;
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

		MeshletGroup group{};
		group.boundingCenter  = glm::vec3(17.0f, 18.0f, 19.0f);
		group.boundingRadius  = 20.0f;
		mesh.meshletGroups    = { group };
		mesh.meshletVertices  = { 5, 6, 7 };
		mesh.meshletTriangles = { 8, 9, 10 };

		mesh.vertexData.resize(3 * 48);
		for (size_t i = 0; i < mesh.vertexData.size(); ++i)
			mesh.vertexData[i] = static_cast<std::byte>(i);
		mesh.indexData = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 },
			               std::byte{ 4 }, std::byte{ 5 }, std::byte{ 6 } };

		mesh.materials         = { "Materials/unit.bmaterial" };
		mesh.skeleton          = "Skeletons/unit.bskel";
		mesh.skeletonSignature = 0x55;
		mesh.skeletonBoneNames = { "hip" };
		mesh.source            = FixedSource();
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
		animations.skeletonBoneNames = { "hip" };

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

	// A gradient with something to average at every level below the first, so a change to the
	// filter or to the space it averages in moves the hash.
	std::vector<std::byte>
	CanaryPixels()
	{
		constexpr uint32_t     c_Size = 16;
		std::vector<std::byte> pixels(static_cast<size_t>(c_Size) * c_Size * 4);
		for (uint32_t y = 0; y < c_Size; ++y)
			for (uint32_t x = 0; x < c_Size; ++x)
			{
				const size_t t = (static_cast<size_t>(y) * c_Size + x) * 4;
				pixels[t + 0]  = static_cast<std::byte>(x * 17);
				pixels[t + 1]  = static_cast<std::byte>(y * 17);
				pixels[t + 2]  = static_cast<std::byte>((x * y) & 0xFF);
				pixels[t + 3]  = static_cast<std::byte>(255 - x * 8);
			}
		return pixels;
	}

	// The canary gradient as linear float, scaled past 1 so the shared exponent has range to use.
	ImageData
	CanaryFloatImage()
	{
		constexpr uint32_t c_Size = 16;

		const std::vector<std::byte> bytes = CanaryPixels();

		ImageData out;
		out.width    = c_Size;
		out.height   = c_Size;
		out.vkFormat = VkFormat::R32G32B32A32_SFLOAT;
		out.pixels   = core::fixed_buffer<std::byte>(bytes.size() * sizeof(float));

		auto* texels = reinterpret_cast<float*>(out.pixels.data());
		for (size_t i = 0; i < bytes.size(); ++i) texels[i] = static_cast<float>(bytes[i]) / 64.0f;

		const uint64_t pitch = static_cast<uint64_t>(c_Size) * 4 * sizeof(float);
		out.subresources.push_back({ 0, pitch, pitch * c_Size });
		return out;
	}

	/**
	 * What the GPU is handed for one role: the stored blocks of a baked map, the transcoded blocks
	 * of a Basis file for the load row, the packed texels of a `kNone` row.
	 */
	std::vector<std::byte>
	EncodedForRole(TextureRole role)
	{
		const TextureEncoding encoding = textureEncoding(role);

		if (encoding.compression == Ktx2Compression::kNone)
		{
			const ImageData packed = packRgb9e5(CanaryFloatImage());
			return { packed.pixels.data(), packed.pixels.data() + packed.pixels.size() };
		}

		const ImageData       source = rgba8ToImage(CanaryPixels(), 16, 16);
		const Ktx2Compression stored = role == TextureRole::kTranscodeAtLoad ?
		                                   Ktx2Compression::kBasisUASTC :
		                                   encoding.compression;

		const ImageData gpu = decodeKTX2(encodeKTX2(source, false, stored));
		return { gpu.pixels.data(), gpu.pixels.data() + gpu.pixels.size() };
	}
}

TEST_CASE("a writer's output cannot change without its bake token", "[canary][io]")
{
	SECTION(".bmesh")
	{
		CheckCanary(
			AssetCodec<BMesh>::c_BakeToken,
			Pin{ .token = 0x668da118da7846a0ull, .hash = 0x4a322ab6ad88ff51ull },
			AssetCodec<BMesh>::Serialize(CanaryMesh()));
	}

	SECTION(".bskel")
	{
		CheckCanary(
			AssetCodec<Skeleton>::c_BakeToken,
			Pin{ .token = 0x9be47d02a15c68f3ull, .hash = 0x3dd4d201c9b7ea0bull },
			AssetCodec<Skeleton>::Serialize(CanarySkeleton()));
	}

	SECTION(".banim")
	{
		CheckCanary(
			AssetCodec<AnimationSet>::c_BakeToken,
			Pin{ .token = 0xc72f38da695b104eull, .hash = 0x12ba29bd4c5dafd1ull },
			AssetCodec<AnimationSet>::Serialize(CanaryAnimations()));
	}

	SECTION(".bsky")
	{
		CheckCanary(
			AssetCodec<BSky>::c_BakeToken,
			Pin{ .token = 0xe4953f8c7c481c07ull, .hash = 0x137f4544ff950969ull },
			AssetCodec<BSky>::Serialize(CanarySky()));
	}

	SECTION(".benvl")
	{
		CheckCanary(
			AssetCodec<BEnvLighting>::c_BakeToken,
			Pin{ .token = 0xde4ee8df9d425a20ull, .hash = 0xc0ece8c4cca39e31ull },
			AssetCodec<BEnvLighting>::Serialize(CanaryLighting()));
	}

	SECTION(".ktx2 mips")
	{
		// Not a codec's: the chain rgba8ToImage writes, which a .ktx2 cannot carry a token for, so
		// the documents that own one carry c_TextureBakeToken instead (docs/asset_containers.md).
		// All three chains it writes: colour in linear light, data as stored, and a cutout's
		// coverage-preserving alpha. Any one of them moving under the same token is the failure.
		const auto bytes = [](const ImageData& image) {
			return std::span<const std::byte>(image.pixels.data(), image.pixels.size());
		};
		CheckCanary(
			c_TextureBakeToken,
			Pin{ .token = 0x4f1a83c05e7b29d6ull, .hash = 0x1e421995f2192fa1ull },
			bytes(rgba8ToImage(CanaryPixels(), 16, 16, std::nullopt, /*srgb*/ true)));
		CheckCanary(
			c_TextureBakeToken,
			Pin{ .token = 0x4f1a83c05e7b29d6ull, .hash = 0x0a2c7e2ccb9e07e3ull },
			bytes(rgba8ToImage(CanaryPixels(), 16, 16)));
		CheckCanary(
			c_TextureBakeToken,
			Pin{ .token = 0x4f1a83c05e7b29d6ull, .hash = 0x2032a96f19d50a48ull },
			bytes(rgba8ToImage(CanaryPixels(), 16, 16, 0.5f, /*srgb*/ true)));
	}

	SECTION(".ktx2 encoding")
	{
		// Every row of the table -- its role, its tag, and the bytes the GPU receives for the
		// canary chain -- under the one token. A changed row and a libktx upgrade that moves the
		// blocks both fail here. The pin assumes basisu encodes identically on every platform the
		// suite runs on.
		auto rows = std::vector<std::byte>();
		for (auto r = 0u; r < static_cast<uint32_t>(TextureRole::kCount); ++r)
		{
			const auto             role = static_cast<TextureRole>(r);
			const std::string_view tag  = textureEncoding(role).tag;

			rows.push_back(static_cast<std::byte>(r));
			for (const char c : tag) rows.push_back(static_cast<std::byte>(c));
			rows.push_back(std::byte{ 0 });

			const std::vector<std::byte> encoded = EncodedForRole(role);
			rows.insert(rows.end(), encoded.begin(), encoded.end());
		}
		CheckCanary(
			c_TextureEncodingToken,
			Pin{ .token = 0x5f497f4931e9cfb8ull, .hash = 0x14df57c5d10d3339ull },
			rows);
	}
}
