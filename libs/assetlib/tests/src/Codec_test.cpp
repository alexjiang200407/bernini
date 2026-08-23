#include <assetlib/AssetCodec.h>
#include <assetlib/AssetStore.h>

#include <assetlib/banim_io.h>
#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/bvat_io.h>
#include <assetlib/import_document.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/Skeleton.h>

#include "RefsSandbox.h"

using namespace assetlib;

namespace
{
	/**
	 * Save through the codec, read the file back raw, and compare it against what the container's
	 * own serializer produces.
	 *
	 * This is the whole point of the task: the codec surface and the free functions must agree
	 * *before* anything migrates onto the codec, because after that there is nothing left to
	 * compare against. A codec wired to the wrong serializer still round-trips through itself
	 * perfectly, so a round trip alone would not catch it -- the byte comparison against the other
	 * surface is what does.
	 */
	template <AssetCodecFor T, typename SerializeFn>
	void
	CheckAgreesWithSerializer(const T& value, std::string_view key, SerializeFn&& serialize)
	{
		assetlib::test::DataRoot root("assetlib_codec_test");
		const AssetStore         store(root.path);

		store.Save(value, key);

		const std::vector<std::byte> onDisk   = store.GetFiles().Read(key);
		const std::vector<std::byte> expected = serialize(value);
		CHECK(onDisk == expected);

		// And the codec reads back what it wrote, through the same key it was written by.
		CHECK_NOTHROW(store.Load<T>(key));
	}
}

TEST_CASE("Every container agrees with its own serializer", "[codec]")
{
	SECTION("bmesh")
	{
		BMesh mesh;
		mesh.roots = { 0 };
		CheckAgreesWithSerializer(mesh, "a.bmesh", [](const BMesh& v) { return serialize(v); });
	}

	SECTION("bmaterial")
	{
		BMaterial material;
		material.name = "brick";
		CheckAgreesWithSerializer(material, "a.bmaterial", [](const BMaterial& v) {
			return serializeMaterial(v);
		});
	}

	SECTION("bskel")
	{
		Skeleton skeleton;
		CheckAgreesWithSerializer(skeleton, "a.bskel", [](const Skeleton& v) {
			return serializeSkeleton(v);
		});
	}

	SECTION("banim")
	{
		AnimationSet animations;
		CheckAgreesWithSerializer(animations, "a.banim", [](const AnimationSet& v) {
			return serializeAnimations(v);
		});
	}

	SECTION("bvat")
	{
		// Payloads, not a default: `serializeVat` refuses an empty pair outright, since that is
		// what a tables-only read leaves behind and writing it back would silently lose the texels.
		BVat vat;
		vat.positionsKtx2 = { std::byte{ 1 }, std::byte{ 2 } };
		vat.normalsKtx2   = { std::byte{ 3 }, std::byte{ 4 } };
		CheckAgreesWithSerializer(vat, "a.bvat", [](const BVat& v) { return serializeVat(v); });
	}

	SECTION("bsky")
	{
		BSky sky;
		CheckAgreesWithSerializer(sky, "a.bsky", [](const BSky& v) { return serializeSky(v); });
	}

	SECTION("benvl")
	{
		BEnvLighting lighting;
		CheckAgreesWithSerializer(lighting, "a.benvl", [](const BEnvLighting& v) {
			return serializeEnvLighting(v);
		});
	}

	SECTION("benv")
	{
		BEnv env;
		CheckAgreesWithSerializer(env, "a.benv", [](const BEnv& v) { return serializeEnv(v); });
	}

	SECTION("bimport")
	{
		// The one container whose serializer yields text rather than bytes, so its codec adapts.
		// Comparing against that text's bytes is what proves the adaptation is a reinterpretation
		// and not a re-encoding.
		ImportDocument document;
		CheckAgreesWithSerializer(document, "a.bimport", [](const ImportDocument& v) {
			const std::string      text = serializeImportDocument(v);
			std::vector<std::byte> bytes(text.size());
			std::memcpy(bytes.data(), text.data(), text.size());
			return bytes;
		});
	}
}

TEST_CASE("The container table is the only list", "[codec]")
{
	const std::span<const ContainerKind> kinds = containerKinds();

	// One entry per container, and every AssetType but the texture -- which is an image this
	// library encodes rather than a container it serializes a struct into.
	CHECK(kinds.size() == static_cast<size_t>(AssetType::kImportDocument));
	CHECK_FALSE(containerKindForExtension(c_TextureExtension).has_value());

	SECTION("every extension resolves to the type its codec declares")
	{
		for (const ContainerKind& kind : kinds)
		{
			const std::optional<ContainerKind> found = containerKindForExtension(kind.extension);
			REQUIRE(found.has_value());
			CHECK(found->type == kind.type);
			CHECK(containerKindFor(kind.type).extension == kind.extension);
		}
	}

	SECTION("an extension this library does not store resolves to nothing")
	{
		CHECK_FALSE(containerKindForExtension(".txt").has_value());
		CHECK_FALSE(containerKindForExtension(".glb").has_value());
		CHECK_FALSE(containerKindForExtension(c_PakExtension).has_value());
	}

	SECTION("the cache entries are exactly the ones carrying a bake token")
	{
		// A document has no bake revision, so a token on one -- or a cache entry without one --
		// means the writer and the regeneration seam disagree about which family it is in.
		CHECK(containerKindFor(AssetType::kMesh).IsCacheEntry());
		CHECK(containerKindFor(AssetType::kSkeleton).IsCacheEntry());
		CHECK(containerKindFor(AssetType::kAnimation).IsCacheEntry());
		CHECK(containerKindFor(AssetType::kVat).IsCacheEntry());
		CHECK(containerKindFor(AssetType::kSky).IsCacheEntry());
		CHECK(containerKindFor(AssetType::kEnvLighting).IsCacheEntry());

		CHECK_FALSE(containerKindFor(AssetType::kMaterial).IsCacheEntry());
		CHECK_FALSE(containerKindFor(AssetType::kEnvironment).IsCacheEntry());
		CHECK_FALSE(containerKindFor(AssetType::kImportDocument).IsCacheEntry());
	}
}

TEST_CASE("Save refuses a key that escapes the data root", "[codec]")
{
	assetlib::test::DataRoot root("assetlib_codec_escape_test");
	const AssetStore         store(root.path);

	BMaterial material;

	// The failure the seam exists to prevent, and nothing pinned it before: a key is
	// data-root-relative by definition, so one that climbs out is refused rather than written.
	CHECK_THROWS_AS(store.Save(material, "../escaped.bmaterial"), std::runtime_error);
	CHECK_THROWS_AS(store.Save(material, "/etc/passwd"), std::runtime_error);
}
