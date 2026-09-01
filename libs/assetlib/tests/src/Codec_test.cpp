#include <assetlib/AssetStore.h>
#include <assetlib/avatar.h>
#include <assetlib/codecs.h>

#include <assetlib/import_document.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include "RefsSandbox.h"

using namespace assetlib;

namespace
{
	/**
	 * Save through the store, read the file back raw, and compare it against the codec's own bytes.
	 *
	 * What this pins is that the store adds nothing on the way out: no wrapper, no re-encode, no
	 * second opinion about which bytes a container is. A round trip alone would not catch a store
	 * that wrapped what it wrote, because it would unwrap it again on the way back in.
	 *
	 * It used to compare the codec against the container's free `serialize*` instead -- the two
	 * surfaces had to agree while callers were migrating between them. Those functions are gone,
	 * and a codec compared against itself proves nothing, so the comparison moved to the seam that
	 * still has two sides.
	 */
	template <AssetCodecFor T>
	void
	CheckStoreWritesCodecBytes(const T& value, const std::string_view leaf)
	{
		assetlib::test::DataRoot root("assetlib_codec_test");
		const AssetStore         store(root.path);

		// The half rather than a category: these cases pin the codec's bytes, and the origin is the
		// whole of what Save asks about.
		const std::string key = KeyIn(
			originFor<T>() == AssetOrigin::kDerived ? c_DerivedDirectoryName :
													  c_AuthoredDirectoryName,
			leaf);

		store.Save(value, key);

		CHECK(store.GetFiles().Read(key) == AssetCodec<T>::Serialize(value));

		// And the codec reads back what it wrote, through the same key it was written by.
		CHECK_NOTHROW(store.Load<T>(key));
	}
}

TEST_CASE("The store writes exactly what the codec encodes", "[codec]")
{
	SECTION("bmesh")
	{
		BMesh mesh;
		mesh.roots = { 0 };
		CheckStoreWritesCodecBytes(mesh, "a.bmesh");
	}

	SECTION("bmaterial")
	{
		BMaterial material;
		material.name = "brick";
		CheckStoreWritesCodecBytes(material, "a.bmaterial");
	}

	SECTION("bskel")
	{
		Skeleton skeleton;
		CheckStoreWritesCodecBytes(skeleton, "a.bskel");
	}

	SECTION("banim")
	{
		AnimationSet animations;
		CheckStoreWritesCodecBytes(animations, "a.banim");
	}

	SECTION("bsky")
	{
		BSky sky;
		CheckStoreWritesCodecBytes(sky, "a.bsky");
	}

	SECTION("benvl")
	{
		BEnvLighting lighting;
		CheckStoreWritesCodecBytes(lighting, "a.benvl");
	}

	SECTION("benv")
	{
		BEnv env;
		CheckStoreWritesCodecBytes(env, "a.benv");
	}

	SECTION("bimport")
	{
		// The one container a person edits by hand: its bytes are the canonical JSON verbatim.
		ImportDocument document;
		CheckStoreWritesCodecBytes(document, "a.bimport");
	}

	SECTION("bavatar")
	{
		Avatar avatar;
		CheckStoreWritesCodecBytes(avatar, "a.bavatar");
	}
}

TEST_CASE("The container table is the only list", "[codec]")
{
	const std::span<const ContainerKind> kinds = containerKinds();

	// One entry per container, and every AssetType but the texture -- which is an image this
	// library encodes rather than a container it serializes a struct into. Counted off kCount, not
	// off whichever enumerator happens to be last: naming one makes appending a type quietly
	// rewrite what this asserts.
	CHECK(kinds.size() == static_cast<size_t>(AssetType::kCount) - 1);
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

TEST_CASE("Save creates the directories its key names", "[codec]")
{
	assetlib::test::DataRoot root("assetlib_codec_mkdir_test");
	const AssetStore         store(root.path);

	BMaterial material;
	material.name = "brick";

	// A key is a location in the data root, not a location that exists: an import aimed at a
	// subfolder writes two levels of it that nothing scaffolded. Without this the write fails
	// inside write_atomic, naming a temp file, which reads as a permissions problem.
	REQUIRE_FALSE(std::filesystem::exists(root.path / "Authored/Materials" / "walls"));
	store.Save(material, "Authored/Materials/walls/brick.bmaterial");

	CHECK(std::filesystem::exists(root.path / "Authored/Materials" / "walls" / "brick.bmaterial"));
	CHECK(store.Load<BMaterial>("Authored/Materials/walls/brick.bmaterial").name == "brick");

	SECTION("a key that escapes is still refused, before any directory is made")
	{
		CHECK_THROWS_AS(store.Save(material, "../up/escaped.bmaterial"), std::runtime_error);
		CHECK_FALSE(std::filesystem::exists(root.path.parent_path() / "up"));
	}
}
